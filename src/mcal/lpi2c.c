/*
 * lpi2c.c - LPI2C0 master, clock FIRCDIV2 = 48 MHz
 *
 * Thoi gian SCL (RM LPI2C): T_SCL = (CLKLO + 1 + CLKHI + 1 + SCL_LATENCY) / f_presc
 *   SCL_LATENCY = ceil((2 + FILTSCL) / 2^PRESCALE), FILTSCL = 0
 *   100 kHz: PRESCALE=2 (12 MHz, 83 ns) -> CLKLO=63, CLKHI=54 -> 64+55+1 = 120 -> 100 kHz
 *            t_LOW = 5,3 us (>= 4,7), t_HIGH = 4,6 us (>= 4,0)
 *   400 kHz: PRESCALE=1 (24 MHz, 42 ns) -> CLKLO=32, CLKHI=25 -> 33+26+1 = 60 -> 400 kHz
 *            t_LOW = 1,38 us (>= 1,3), t_HIGH = 1,08 us (>= 0,6)
 *
 * Lenh MTDR (CMD):
 *   0 = gui DATA      1 = nhan (DATA+1) byte      2 = STOP
 *   4 = START + gui dia chi (DATA = addr<<1 | R/W)
 *
 * Thanh ghi dung: PCC, PORTA PCR, MCR, MCFGR1, MCFGR2, MCCR0, MFCR, MSR, MTDR, MRDR
 */
#include <stddef.h>
#include "S32K144.h"
#include "lpi2c.h"
#include "../app_cfg.h"

#define CMD_TX          0u
#define CMD_RX          1u
#define CMD_STOP        2u
#define CMD_START       4u
#define I2C_READ_BIT    1u
#define RX_LEN_MAX      256u

#define MSR_ERR_MASK    (LPI2C_MSR_NDF_MASK | LPI2C_MSR_ALF_MASK | \
                         LPI2C_MSR_FEF_MASK | LPI2C_MSR_PLTF_MASK)
#define MSR_W1C_MASK    (MSR_ERR_MASK | LPI2C_MSR_SDF_MASK | LPI2C_MSR_EPF_MASK | \
                         LPI2C_MSR_DMF_MASK)

typedef struct
{
    uint32_t khz;
    uint32_t prescale;
    uint32_t clklo;
    uint32_t clkhi;
    uint32_t sethold;
    uint32_t datavd;
} i2c_timing_t;

static const i2c_timing_t s_timing[] =
{
    { 100u, 2u, 63u, 54u, 55u, 10u },
    { 400u, 1u, 32u, 25u, 16u,  5u }
};

static uint32_t    s_khz;
static i2c_stats_t s_st;

#define PORT_MUX_GPIO      1u
#define BUS_CLEAR_PULSES   9u
#define BUS_HALF_PERIOD    200u     /* Vong cho ~5 us -> ~100 kHz */

static void BusDelay(void)
{
    volatile uint32_t n = BUS_HALF_PERIOD;
    while (n > 0u) { n--; }
}

/* Go ket bus (UM10204 3.1.16): neu slave dang giu SDA thap (vd MCU bi reset
   giua luc doc), phat toi da 9 xung SCL cho slave nha SDA, roi tao STOP.
   SCL/SDA dieu khien nhu open-drain: muc 0 = xuat 0, muc 1 = tha (input + keo len). */
static void BusClear(void)
{
    uint32_t scl = 1u << CFG_I2C_SCL_PIN;
    uint32_t sda = 1u << CFG_I2C_SDA_PIN;
    uint32_t i;

    PORTA->PCR[CFG_I2C_SCL_PIN] = PORT_PCR_MUX(PORT_MUX_GPIO);
    PORTA->PCR[CFG_I2C_SDA_PIN] = PORT_PCR_MUX(PORT_MUX_GPIO);
    PTA->PCOR  = scl | sda;                  /* Gia tri xuat = 0 khi la output */
    PTA->PDDR &= ~(scl | sda);               /* Tha ca 2 */
    BusDelay();

    for (i = 0u; (i < BUS_CLEAR_PULSES) && ((PTA->PDIR & sda) == 0u); i++)
    {
        PTA->PDDR |= scl;  BusDelay();       /* SCL = 0 */
        PTA->PDDR &= ~scl; BusDelay();       /* SCL = 1 */
    }
    /* STOP: SDA 0 -> 1 trong khi SCL = 1 */
    PTA->PDDR |= scl;  BusDelay();
    PTA->PDDR |= sda;  BusDelay();
    PTA->PDDR &= ~scl; BusDelay();
    PTA->PDDR &= ~sda; BusDelay();
}

/* ------------------------------------------------------------------ */
status_t I2c_Init(uint32_t speed_khz)
{
    const i2c_timing_t *t = NULL;
    uint32_t i;

    for (i = 0u; i < (sizeof(s_timing) / sizeof(s_timing[0])); i++)
    {
        if (s_timing[i].khz == speed_khz)
        {
            t = &s_timing[i];
        }
    }
    if (t == NULL)
    {
        return STATUS_ERR_PARAM;
    }

    /* 1. Chan (module da co keo len ngoai). Go ket bus truoc khi giao cho LPI2C */
    PCC->PCCn[PCC_PORTA_INDEX] |= PCC_PCCn_CGC_MASK;
    BusClear();
    PORTA->PCR[CFG_I2C_SDA_PIN] = PORT_PCR_MUX(CFG_I2C_PIN_MUX);
    PORTA->PCR[CFG_I2C_SCL_PIN] = PORT_PCR_MUX(CFG_I2C_PIN_MUX);

    /* 2. Clock */
    PCC->PCCn[PCC_LPI2C0_INDEX] = 0u;
    PCC->PCCn[PCC_LPI2C0_INDEX] = PCC_PCCn_PCS(CFG_PCC_PCS_FIRCDIV2);
    PCC->PCCn[PCC_LPI2C0_INDEX] |= PCC_PCCn_CGC_MASK;
    if ((PCC->PCCn[PCC_LPI2C0_INDEX] & PCC_PCCn_PR_MASK) == 0u)
    {
        return STATUS_ERR_HW;
    }

    /* 3. Reset, cau hinh khi MEN = 0 */
    LPI2C0->MCR    = LPI2C_MCR_RST_MASK;
    LPI2C0->MCR    = 0u;
    LPI2C0->MCFGR1 = LPI2C_MCFGR1_PRESCALE(t->prescale);      /* PINCFG 0: 2 day open-drain */
    LPI2C0->MCFGR2 = LPI2C_MCFGR2_FILTSDA(0u) | LPI2C_MCFGR2_FILTSCL(0u) |
                     LPI2C_MCFGR2_BUSIDLE(0u);
    LPI2C0->MCCR0  = LPI2C_MCCR0_CLKLO(t->clklo) | LPI2C_MCCR0_CLKHI(t->clkhi) |
                     LPI2C_MCCR0_SETHOLD(t->sethold) | LPI2C_MCCR0_DATAVD(t->datavd);
    LPI2C0->MFCR   = LPI2C_MFCR_TXWATER(0u) | LPI2C_MFCR_RXWATER(0u);
    LPI2C0->MSR    = MSR_W1C_MASK;
    LPI2C0->MCR    = LPI2C_MCR_MEN_MASK | LPI2C_MCR_DBGEN_MASK |
                     LPI2C_MCR_RTF_MASK | LPI2C_MCR_RRF_MASK;

    s_khz = t->khz;
    return STATUS_OK;
}

uint32_t I2c_GetSpeedKhz(void)
{
    return s_khz;
}

/* ------------------------------------------------------------------ */
/* Xu ly loi chung: NACK / mat quyen bus -> xoa FIFO, phat STOP neu can */
static status_t Recover(uint32_t msr)
{
    if ((msr & LPI2C_MSR_NDF_MASK) != 0u)
    {
        s_st.nack++;
    }
    if ((msr & LPI2C_MSR_ALF_MASK) != 0u)
    {
        s_st.arb_lost++;
    }
    LPI2C0->MCR |= LPI2C_MCR_RTF_MASK | LPI2C_MCR_RRF_MASK;
    if ((LPI2C0->MSR & LPI2C_MSR_MBF_MASK) != 0u)
    {
        LPI2C0->MTDR = LPI2C_MTDR_CMD(CMD_STOP);
    }
    LPI2C0->MSR = MSR_W1C_MASK;
    return ((msr & LPI2C_MSR_NDF_MASK) != 0u) ? STATUS_ERR_HW : STATUS_ERR_BUSY;
}

/* Day 1 lenh vao TX FIFO (cho cho trong) */
static status_t Push(uint32_t cmd, uint32_t data)
{
    uint32_t timeout = CFG_I2C_TIMEOUT;
    uint32_t msr;

    for (;;)
    {
        msr = LPI2C0->MSR;
        if ((msr & MSR_ERR_MASK) != 0u)
        {
            return Recover(msr);
        }
        if ((msr & LPI2C_MSR_TDF_MASK) != 0u)
        {
            break;
        }
        if (--timeout == 0u)
        {
            s_st.timeout++;
            return STATUS_ERR_TIMEOUT;
        }
    }
    LPI2C0->MTDR = LPI2C_MTDR_CMD(cmd) | LPI2C_MTDR_DATA(data);
    return STATUS_OK;
}

/* Cho STOP xong (SDF) hoac loi */
static status_t WaitStop(void)
{
    uint32_t timeout = CFG_I2C_TIMEOUT;
    uint32_t msr;

    for (;;)
    {
        msr = LPI2C0->MSR;
        if ((msr & MSR_ERR_MASK) != 0u)
        {
            return Recover(msr);
        }
        if ((msr & LPI2C_MSR_SDF_MASK) != 0u)
        {
            LPI2C0->MSR = LPI2C_MSR_SDF_MASK;
            return STATUS_OK;
        }
        if (--timeout == 0u)
        {
            s_st.timeout++;
            (void)Recover(msr);
            return STATUS_ERR_TIMEOUT;
        }
    }
}

static status_t Begin(void)
{
    if ((LPI2C0->MSR & LPI2C_MSR_BBF_MASK) != 0u)
    {
        /* Bus dang ban (vd mot giao dich cu bi treo) -> thu giai phong */
        (void)Recover(LPI2C0->MSR);
    }
    LPI2C0->MSR = MSR_W1C_MASK;
    s_st.transfers++;
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
status_t I2c_Probe(uint8_t addr)
{
    status_t st = Begin();

    if (st == STATUS_OK) { st = Push(CMD_START, (uint32_t)addr << 1); }
    if (st == STATUS_OK) { st = Push(CMD_STOP, 0u); }
    if (st == STATUS_OK) { st = WaitStop(); }
    return st;
}

status_t I2c_WriteReg(uint8_t addr, uint8_t reg, const uint8_t *data, uint32_t len)
{
    status_t st = Begin();
    uint32_t i;

    if (st == STATUS_OK) { st = Push(CMD_START, (uint32_t)addr << 1); }
    if (st == STATUS_OK) { st = Push(CMD_TX, reg); }
    for (i = 0u; (st == STATUS_OK) && (i < len); i++)
    {
        st = Push(CMD_TX, data[i]);
    }
    if (st == STATUS_OK) { st = Push(CMD_STOP, 0u); }
    if (st == STATUS_OK) { st = WaitStop(); }
    return st;
}

status_t I2c_ReadReg(uint8_t addr, uint8_t reg, uint8_t *data, uint32_t len)
{
    status_t st;
    uint32_t i;
    uint32_t timeout;
    uint32_t rdr;

    if ((data == NULL) || (len == 0u) || (len > RX_LEN_MAX))
    {
        return STATUS_ERR_PARAM;
    }
    st = Begin();
    if (st == STATUS_OK) { st = Push(CMD_START, (uint32_t)addr << 1); }                   /* Ghi */
    if (st == STATUS_OK) { st = Push(CMD_TX, reg); }
    if (st == STATUS_OK) { st = Push(CMD_START, ((uint32_t)addr << 1) | I2C_READ_BIT); }  /* Repeated START, doc */
    if (st == STATUS_OK) { st = Push(CMD_RX, len - 1u); }
    if (st == STATUS_OK) { st = Push(CMD_STOP, 0u); }

    for (i = 0u; (st == STATUS_OK) && (i < len); i++)
    {
        timeout = CFG_I2C_TIMEOUT;
        for (;;)
        {
            rdr = LPI2C0->MRDR;
            if ((rdr & LPI2C_MRDR_RXEMPTY_MASK) == 0u)
            {
                data[i] = (uint8_t)(rdr & LPI2C_MRDR_DATA_MASK);
                break;
            }
            if ((LPI2C0->MSR & MSR_ERR_MASK) != 0u)
            {
                st = Recover(LPI2C0->MSR);
                break;
            }
            if (--timeout == 0u)
            {
                s_st.timeout++;
                st = STATUS_ERR_TIMEOUT;
                break;
            }
        }
    }
    if (st == STATUS_OK)
    {
        st = WaitStop();
    }
    return st;
}

void I2c_GetStats(i2c_stats_t *st)
{
    *st = s_st;
}
