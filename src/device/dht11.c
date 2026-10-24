/*
 * dht11.c - Doc DHT11 bang GPIO PTD13 dang open-drain gia lap:
 *   muc 0 = PDDR=1 (xuat 0) ; muc 1 = PDDR=0 (tha, keo len ngoai + keo len trong)
 */
#include "S32K144.h"
#include "dht11.h"
#include "../mcal/lpit.h"
#include "../app_cfg.h"

#define PORT_MUX_GPIO        1u
#define START_LOW_MS         20u      /* >= 18 ms                          */
#define POWERUP_WAIT_MS      1500u    /* DHT11 can ~1 s on dinh sau cap nguon */
#define RESP_WAIT_US         200u     /* Cho DHT keo xuong sau khi tha     */
#define RESP_PHASE_US        120u     /* Pha 0/1 80 us cua phan hoi        */
#define BIT_LOW_US           100u     /* Pha 0 50 us dau moi bit           */
#define BIT_HIGH_US          120u     /* Pha 1 26..70 us                   */
#define BIT_ONE_THRESH_US    48u      /* > 48 us -> bit 1                  */
#define DATA_BYTES           5u
#define BITS_PER_BYTE        8u
#define STALE_MS             (3u * CFG_DHT_PERIOD_MS)

#define ERR_STAGE_RESP0      1u
#define ERR_STAGE_RESP1      2u
#define ERR_STAGE_RESP2      3u
#define ERR_STAGE_BITS       4u

typedef enum
{
    DHT_ST_WAIT_POWER = 0,
    DHT_ST_IDLE,
    DHT_ST_START_LOW
} dht_state_t;

static dht_state_t  s_state;
static uint32_t     s_t0_ms;
static dht11_info_t s_info;

#define PIN_MASK     (1u << CFG_DHT_PIN)

static inline void     PinLow(void)     { CFG_DHT_GPIO->PCOR = PIN_MASK; CFG_DHT_GPIO->PDDR |= PIN_MASK; }
static inline void     PinRelease(void) { CFG_DHT_GPIO->PDDR &= ~PIN_MASK; }
static inline uint32_t PinRead(void)    { return ((CFG_DHT_GPIO->PDIR & PIN_MASK) != 0u) ? 1u : 0u; }

/* Cho trong khi chan con o muc 'level'. Tra ve so us da cho, -1 neu qua han. */
static int32_t WaitWhile(uint32_t level, uint32_t timeout_us)
{
    uint32_t t0 = Tick_GetUs();
    uint32_t dt;

    while (PinRead() == level)
    {
        dt = Tick_GetUs() - t0;
        if ((dt > timeout_us) && (dt < 0x80000000u))     /* Bo qua gia tri am (phong ho) */
        {
            return -1;
        }
    }
    return (int32_t)(Tick_GetUs() - t0);
}

static void ReadFrame(void)
{
    uint8_t  d[DATA_BYTES] = { 0u, 0u, 0u, 0u, 0u };
    uint32_t i;
    uint32_t t0 = Tick_GetUs();
    int32_t  h;
    uint8_t  sum;

    s_info.reads++;
    PinRelease();

    if (WaitWhile(1u, RESP_WAIT_US) < 0)   { s_info.err_timeout++; s_info.last_err_stage = ERR_STAGE_RESP0; return; }
    if (WaitWhile(0u, RESP_PHASE_US) < 0)  { s_info.err_timeout++; s_info.last_err_stage = ERR_STAGE_RESP1; return; }
    if (WaitWhile(1u, RESP_PHASE_US) < 0)  { s_info.err_timeout++; s_info.last_err_stage = ERR_STAGE_RESP2; return; }

    for (i = 0u; i < (DATA_BYTES * BITS_PER_BYTE); i++)
    {
        if (WaitWhile(0u, BIT_LOW_US) < 0)
        {
            s_info.err_timeout++;
            s_info.last_err_stage = ERR_STAGE_BITS;
            return;
        }
        h = WaitWhile(1u, BIT_HIGH_US);
        if (h < 0)
        {
            s_info.err_timeout++;
            s_info.last_err_stage = ERR_STAGE_BITS;
            return;
        }
        d[i / BITS_PER_BYTE] = (uint8_t)(d[i / BITS_PER_BYTE] << 1);
        if ((uint32_t)h > BIT_ONE_THRESH_US)
        {
            d[i / BITS_PER_BYTE] |= 1u;
        }
    }
    s_info.last_read_us = Tick_GetUs() - t0;

    sum = (uint8_t)(d[0] + d[1] + d[2] + d[3]);
    if (sum != d[4])
    {
        s_info.err_checksum++;
        s_info.last_err_stage = ERR_STAGE_BITS;
        return;
    }
    /* DHT11: byte le thuong = 0 (do am) va 0..9 (nhiet); bit7 byte le nhiet = dau am */
    s_info.hum_x10  = (uint16_t)((d[0] * 10u) + (d[1] % 10u));
    s_info.temp_x10 = (int16_t)((d[2] * 10u) + ((d[3] & 0x7Fu) % 10u));
    if ((d[3] & 0x80u) != 0u)
    {
        s_info.temp_x10 = (int16_t)(-s_info.temp_x10);
    }
    s_info.ok++;
    s_info.valid          = true;
    s_info.last_ok_ms     = Tick_GetMs();
    s_info.last_err_stage = 0u;
}

/* ------------------------------------------------------------------ */
status_t Dht11_Init(void)
{
    PCC->PCCn[CFG_DHT_PCC_INDEX] |= PCC_PCCn_CGC_MASK;
    CFG_DHT_PORT->PCR[CFG_DHT_PIN] = PORT_PCR_MUX(PORT_MUX_GPIO) |
                                     PORT_PCR_PE_MASK | PORT_PCR_PS_MASK;   /* Keo len trong */
    PinRelease();
    s_state = DHT_ST_WAIT_POWER;
    s_t0_ms = Tick_GetMs();
    return STATUS_OK;
}

void Dht11_Task(void)
{
    uint32_t now = Tick_GetMs();

    switch (s_state)
    {
    case DHT_ST_WAIT_POWER:
        if ((now - s_t0_ms) >= POWERUP_WAIT_MS)
        {
            s_state = DHT_ST_IDLE;
            s_t0_ms = now - CFG_DHT_PERIOD_MS;      /* Doc ngay lan dau */
        }
        break;

    case DHT_ST_IDLE:
        if ((now - s_t0_ms) >= CFG_DHT_PERIOD_MS)
        {
            s_t0_ms = now;
            PinLow();                                /* Tin hieu START */
            s_state = DHT_ST_START_LOW;
        }
        break;

    case DHT_ST_START_LOW:
        if ((now - s_t0_ms) >= START_LOW_MS)
        {
            ReadFrame();                             /* ~4-5 ms */
            PinRelease();
            s_state = DHT_ST_IDLE;
        }
        break;

    default:
        s_state = DHT_ST_IDLE;
        break;
    }

    now = Tick_GetMs();                              /* ReadFrame co the vua cap nhat last_ok_ms */
    if (s_info.valid && ((now - s_info.last_ok_ms) > STALE_MS))
    {
        s_info.valid = false;                        /* Rut cam bien -> bao mat du lieu */
    }
}

bool Dht11_Get(int16_t *temp_x10, uint16_t *hum_x10)
{
    if (!s_info.valid)
    {
        return false;
    }
    *temp_x10 = s_info.temp_x10;
    *hum_x10  = s_info.hum_x10;
    return true;
}

void Dht11_GetInfo(dht11_info_t *info)
{
    *info = s_info;
}
