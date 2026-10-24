/*
 * lpspi.c - LPSPI0 master muc thanh ghi
 *
 * Clock: PCC PCS = FIRCDIV2 (48 MHz), PRESCALE = /1
 *   f_SCK = 48 MHz / (SCKDIV + 2)   -> 4 MHz: SCKDIV = 10, 1 MHz: SCKDIV = 46
 *
 * Thanh ghi dung:
 *   CR    : MEN, RST, RTF/RRF (xoa FIFO), DBGEN
 *   CFGR1 : MASTER, PINCFG (dao SIN/SOUT)
 *   CCR   : SCKDIV, DBT, PCSSCK, SCKPCS
 *   TCR   : CPOL = 0, CPHA = 0 (mode 0), FRAMESZ = 7 (8 bit), PRESCALE
 *   TDR/RDR, SR (TDF), RSR (RXEMPTY)
 * CS: GPIO (PTB5 + PTB0 song song), muc thap = chon chip.
 */
#include <stddef.h>
#include "S32K144.h"
#include "lpspi.h"
#include "../app_cfg.h"

#define PORT_MUX_GPIO     1u
#define SPI_FRAME_8BIT    7u          /* FRAMESZ = bits - 1 */
#define SPI_SCKDIV_OFFSET 2u
#define SPI_SCKDIV_MAX    255u
#define SPI_DUMMY         0xFFu

#define CS_MASK  ((1u << CFG_SPI_CS_PIN) | (1u << CFG_SPI_CS_ALT_PIN))

static uint32_t     s_sck_hz;
static spi_pincfg_t s_pincfg;

/* ------------------------------------------------------------------ */
static uint32_t CalcSckDiv(uint32_t sck_hz)
{
    uint32_t div = (CFG_PERIPH_CLK_HZ + sck_hz - 1u) / sck_hz;   /* Lam tron len: khong vuot tan so */

    if (div < SPI_SCKDIV_OFFSET)
    {
        div = SPI_SCKDIV_OFFSET;
    }
    div -= SPI_SCKDIV_OFFSET;
    return (div > SPI_SCKDIV_MAX) ? SPI_SCKDIV_MAX : div;
}

status_t Spi_SetSpeed(uint32_t sck_hz)
{
    uint32_t div;
    uint32_t cr;

    if (sck_hz == 0u)
    {
        return STATUS_ERR_PARAM;
    }
    div = CalcSckDiv(sck_hz);
    cr  = LPSPI0->CR;

    LPSPI0->CR  = cr & ~LPSPI_CR_MEN_MASK;          /* CCR chi ghi khi MEN = 0 */
    LPSPI0->CCR = LPSPI_CCR_SCKDIV(div) | LPSPI_CCR_DBT(div) |
                  LPSPI_CCR_PCSSCK(div / 2u) | LPSPI_CCR_SCKPCS(div / 2u);
    LPSPI0->CR  = cr;

    s_sck_hz = CFG_PERIPH_CLK_HZ / (div + SPI_SCKDIV_OFFSET);
    return STATUS_OK;
}

status_t Spi_Init(uint32_t sck_hz, spi_pincfg_t pincfg)
{
    status_t st;

    /* 1. Chan: SCK, SIN, SOUT (+ SOUT phu) = ALT3; CS = GPIO ra, muc cao */
    PCC->PCCn[PCC_PORTB_INDEX] |= PCC_PCCn_CGC_MASK;
    PORTB->PCR[CFG_SPI_SCK_PIN]      = PORT_PCR_MUX(CFG_SPI_PIN_MUX);
    PORTB->PCR[CFG_SPI_SIN_PIN]      = PORT_PCR_MUX(CFG_SPI_PIN_MUX);
    PORTB->PCR[CFG_SPI_SOUT_PIN]     = PORT_PCR_MUX(CFG_SPI_PIN_MUX);
    PORTB->PCR[CFG_SPI_SOUT_ALT_PIN] = PORT_PCR_MUX(CFG_SPI_PIN_MUX);
    PORTB->PCR[CFG_SPI_CS_PIN]       = PORT_PCR_MUX(PORT_MUX_GPIO);
    PORTB->PCR[CFG_SPI_CS_ALT_PIN]   = PORT_PCR_MUX(PORT_MUX_GPIO);
    PTB->PSOR  = CS_MASK;
    PTB->PDDR |= CS_MASK;

    /* 2. Clock (PCS ghi khi CGC = 0) */
    PCC->PCCn[PCC_LPSPI0_INDEX] = 0u;
    PCC->PCCn[PCC_LPSPI0_INDEX] = PCC_PCCn_PCS(CFG_PCC_PCS_FIRCDIV2);
    PCC->PCCn[PCC_LPSPI0_INDEX] |= PCC_PCCn_CGC_MASK;
    if ((PCC->PCCn[PCC_LPSPI0_INDEX] & PCC_PCCn_PR_MASK) == 0u)
    {
        return STATUS_ERR_HW;
    }

    /* 3. Reset module, xoa FIFO */
    LPSPI0->CR = LPSPI_CR_RST_MASK;
    LPSPI0->CR = 0u;
    LPSPI0->CR = LPSPI_CR_RTF_MASK | LPSPI_CR_RRF_MASK;

    /* 4. Master, chon cau hinh chan */
    LPSPI0->CFGR1 = LPSPI_CFGR1_MASTER_MASK | LPSPI_CFGR1_PINCFG((uint32_t)pincfg);
    s_pincfg = pincfg;

    st = Spi_SetSpeed(sck_hz);
    if (st != STATUS_OK)
    {
        return st;
    }

    LPSPI0->FCR = LPSPI_FCR_TXWATER(0u) | LPSPI_FCR_RXWATER(0u);
    LPSPI0->CR  = LPSPI_CR_MEN_MASK | LPSPI_CR_DBGEN_MASK;

    /* 5. Lenh truyen: mode 0, 8 bit, MSB truoc, PRESCALE /1 */
    LPSPI0->TCR = LPSPI_TCR_CPOL(0u) | LPSPI_TCR_CPHA(0u) |
                  LPSPI_TCR_PRESCALE(0u) | LPSPI_TCR_FRAMESZ(SPI_FRAME_8BIT);

    if ((LPSPI0->CFGR1 & LPSPI_CFGR1_MASTER_MASK) == 0u)
    {
        return STATUS_ERR_HW;
    }
    return STATUS_OK;
}

uint32_t     Spi_GetSpeed(void)  { return s_sck_hz; }
spi_pincfg_t Spi_GetPinCfg(void) { return s_pincfg; }

void Spi_CsLow(void)  { PTB->PCOR = CS_MASK; }
void Spi_CsHigh(void) { PTB->PSOR = CS_MASK; }

/* ------------------------------------------------------------------ */
status_t Spi_Transfer(uint8_t tx, uint8_t *rx)
{
    uint32_t timeout = CFG_SPI_BYTE_TIMEOUT;
    uint8_t  data;

    while (((LPSPI0->SR & LPSPI_SR_TDF_MASK) == 0u) && (timeout > 0u))
    {
        timeout--;
    }
    if (timeout == 0u)
    {
        return STATUS_ERR_TIMEOUT;
    }
    LPSPI0->TDR = tx;

    timeout = CFG_SPI_BYTE_TIMEOUT;
    while (((LPSPI0->RSR & LPSPI_RSR_RXEMPTY_MASK) != 0u) && (timeout > 0u))
    {
        timeout--;
    }
    if (timeout == 0u)
    {
        return STATUS_ERR_TIMEOUT;
    }
    data = (uint8_t)LPSPI0->RDR;
    if (rx != NULL)
    {
        *rx = data;
    }
    return STATUS_OK;
}

status_t Spi_Write(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    status_t st;

    for (i = 0u; i < len; i++)
    {
        st = Spi_Transfer(data[i], NULL);
        if (st != STATUS_OK)
        {
            return st;
        }
    }
    return STATUS_OK;
}

status_t Spi_Read(uint8_t *data, uint32_t len)
{
    uint32_t i;
    status_t st;

    for (i = 0u; i < len; i++)
    {
        st = Spi_Transfer(SPI_DUMMY, &data[i]);
        if (st != STATUS_OK)
        {
            return st;
        }
    }
    return STATUS_OK;
}
