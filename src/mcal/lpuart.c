/*
 * lpuart.c - Driver LPUART muc thanh ghi, nhieu cong, RX/TX bang ngat + ring buffer
 *
 * Board MaaZEDU khong ghi ro UART nao noi ra dau, nen driver bat dong thoi
 * cac cong ung vien (xem bang s_port[]):
 *   - Truoc khi khoa: moi chuoi gui ra duoc phat tren TAT CA cong.
 *   - Byte dau tien nhan duoc tren cong nao -> khoa console vao cong do,
 *     cac cong con lai ngung phat.
 *
 * Thanh ghi dung (moi cong):
 *   PCC->PCCn[...]   : chon nguon clock (PCS) + bat clock (CGC)
 *   PORTx->PCR[n]    : pin mux
 *   LPUARTx->BAUD    : OSR + SBR
 *   LPUARTx->CTRL    : TE, RE, RIE, TIE
 *   LPUARTx->STAT    : RDRF, TDRE, OR, NF, FE (co loi la W1C)
 *   LPUARTx->DATA    : thanh ghi du lieu
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "S32K144.h"
#include "lpuart.h"
#include "nvic.h"
#include "../service/ring_buffer.h"
#include "../app_cfg.h"

#define UART_PRINTF_BUF   128u
#define PERMILLE          1000u
#define PIN_NONE          0xFFu

#define UART_ERR_FLAGS    (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | \
                           LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK)

/* ---------- Mo ta phan cung tung cong ---------- */
typedef struct
{
    PORT_Type   *port;
    uint8_t      pin;
    uint8_t      mux;
} pin_cfg_t;

typedef struct
{
    const char  *name;
    LPUART_Type *base;
    uint32_t     pcc_index;
    IRQn_Type    irq;
    pin_cfg_t    rx;
    pin_cfg_t    tx;
    pin_cfg_t    tx_alt;      /* Chan TX phu (PIN_NONE neu khong co) */
} uart_port_t;

static const uart_port_t s_port[UART_PORT_COUNT] =
{
    /* J-Link VCOM kieu EVB (OpenSDA dung PTC6/PTC7) */
    { "LPUART1(PTC6/PTC7)", LPUART1, PCC_LPUART1_INDEX, LPUART1_RxTx_IRQn,
      { PORTC, 6u, 2u }, { PORTC, 7u, 2u }, { PORTC, PIN_NONE, 0u } },
    /* Header J8-14/J8-16 (LPUART2) */
    { "LPUART2(PTD6/PTD7,PTA9)", LPUART2, PCC_LPUART2_INDEX, LPUART2_RxTx_IRQn,
      { PORTD, 6u, 2u }, { PORTD, 7u, 2u }, { PORTA, 9u, 2u } }
};
/* LPUART0 (PTB0/PTB1) da bo: 2 chan nay dung chung voi LPSPI0 cua Flash */

/* ---------- Trang thai ---------- */
static uint8_t       s_rx_storage[CFG_UART_RX_BUF_SIZE];
static uint8_t       s_tx_storage[UART_PORT_COUNT][CFG_UART_TX_BUF_SIZE];
static ring_buffer_t s_rx;
static ring_buffer_t s_tx[UART_PORT_COUNT];

static volatile int32_t  s_active = UART_PORT_NONE;   /* Cong da khoa */
static volatile uint32_t s_rx_bytes;
static volatile uint32_t s_rx_raw[UART_PORT_COUNT];   /* Moi byte nhan tren tung cong */
static volatile uint32_t s_tx_bytes[UART_PORT_COUNT];
static volatile uint32_t s_err_or;
static volatile uint32_t s_err_fe;
static volatile uint32_t s_err_nf;
static uint32_t          s_baud_actual;

/* ------------------------------------------------------------------ */
static void EnablePortClock(const PORT_Type *port)
{
    if      (port == PORTA) { PCC->PCCn[PCC_PORTA_INDEX] |= PCC_PCCn_CGC_MASK; }
    else if (port == PORTB) { PCC->PCCn[PCC_PORTB_INDEX] |= PCC_PCCn_CGC_MASK; }
    else if (port == PORTC) { PCC->PCCn[PCC_PORTC_INDEX] |= PCC_PCCn_CGC_MASK; }
    else if (port == PORTD) { PCC->PCCn[PCC_PORTD_INDEX] |= PCC_PCCn_CGC_MASK; }
    else                    { PCC->PCCn[PCC_PORTE_INDEX] |= PCC_PCCn_CGC_MASK; }
}

static void SetPin(const pin_cfg_t *p)
{
    if (p->pin != PIN_NONE)
    {
        EnablePortClock(p->port);
        p->port->PCR[p->pin] = PORT_PCR_MUX(p->mux);
    }
}

static status_t InitPort(uint32_t idx, uint32_t sbr)
{
    const uart_port_t *u = &s_port[idx];

    PCC->PCCn[u->pcc_index] = 0u;
    PCC->PCCn[u->pcc_index] = PCC_PCCn_PCS(CFG_PCC_PCS_FIRCDIV2);
    PCC->PCCn[u->pcc_index] |= PCC_PCCn_CGC_MASK;
    if ((PCC->PCCn[u->pcc_index] & PCC_PCCn_PR_MASK) == 0u)
    {
        return STATUS_ERR_HW;
    }

    SetPin(&u->rx);
    /* Pull-up noi cho RX: chan de ho khong bat nhieu (UART ranh = muc 1) */
    u->rx.port->PCR[u->rx.pin] |= PORT_PCR_PE_MASK | PORT_PCR_PS_MASK;
    SetPin(&u->tx);
    SetPin(&u->tx_alt);

    u->base->CTRL = 0u;                          /* Tat TE/RE truoc khi sua BAUD */
    u->base->BAUD = LPUART_BAUD_OSR(CFG_UART_OSR - 1u) | LPUART_BAUD_SBR(sbr);
    u->base->STAT = UART_ERR_FLAGS;              /* 8N1 mac dinh, xoa co loi cu */
    u->base->CTRL = LPUART_CTRL_RE_MASK | LPUART_CTRL_TE_MASK | LPUART_CTRL_RIE_MASK;

    Nvic_EnableIrq((uint32_t)u->irq, CFG_IRQ_PRIO_UART);
    return STATUS_OK;
}

status_t Uart_Init(uint32_t baud)
{
    uint32_t sbr;
    uint32_t err;
    uint32_t i;
    status_t st;

    if (baud == 0u)
    {
        return STATUS_ERR_PARAM;
    }
    if (RingBuf_Init(&s_rx, s_rx_storage, CFG_UART_RX_BUF_SIZE) != STATUS_OK)
    {
        return STATUS_ERR_PARAM;
    }

    sbr = (CFG_PERIPH_CLK_HZ + (CFG_UART_OSR * baud) / 2u) / (CFG_UART_OSR * baud);
    if ((sbr == 0u) || (sbr > (LPUART_BAUD_SBR_MASK >> LPUART_BAUD_SBR_SHIFT)))
    {
        return STATUS_ERR_PARAM;
    }
    s_baud_actual = CFG_PERIPH_CLK_HZ / (CFG_UART_OSR * sbr);
    err = (s_baud_actual > baud) ? (s_baud_actual - baud) : (baud - s_baud_actual);
    if ((err * PERMILLE) / baud > CFG_UART_MAX_ERR_PERMILLE)
    {
        return STATUS_ERR_PARAM;
    }

    for (i = 0u; i < UART_PORT_COUNT; i++)
    {
        if (RingBuf_Init(&s_tx[i], s_tx_storage[i], CFG_UART_TX_BUF_SIZE) != STATUS_OK)
        {
            return STATUS_ERR_PARAM;
        }
        st = InitPort(i, sbr);
        if (st != STATUS_OK)
        {
            return st;
        }
    }
    s_active = UART_PORT_NONE;
    return STATUS_OK;
}

/* ---------------------- ISR dung chung ---------------------------- */
static void Uart_IrqHandler(uint32_t idx)
{
    LPUART_Type *base = s_port[idx].base;
    uint32_t     stat = base->STAT;
    uint8_t      byte;

    if ((stat & UART_ERR_FLAGS) != 0u)
    {
        if ((stat & LPUART_STAT_OR_MASK) != 0u) { s_err_or++; }
        if ((stat & LPUART_STAT_FE_MASK) != 0u) { s_err_fe++; }
        if ((stat & LPUART_STAT_NF_MASK) != 0u) { s_err_nf++; }
        base->STAT = stat & UART_ERR_FLAGS;      /* W1C */
    }

    if ((stat & LPUART_STAT_RDRF_MASK) != 0u)
    {
        byte = (uint8_t)base->DATA;              /* Doc DATA xoa RDRF */
        s_rx_raw[idx]++;
        /* Console bat dau khi nhan phim Enter hop le (khong loi khung) tu BAT KY cong nao.
           Truoc do bo qua byte de tranh nhieu tu chan de ho. */
        if ((s_active == UART_PORT_NONE) &&
            ((stat & (LPUART_STAT_FE_MASK | LPUART_STAT_NF_MASK)) == 0u) &&
            ((byte == (uint8_t)'\r') || (byte == (uint8_t)'\n')))
        {
            s_active = (int32_t)idx;             /* Cong nhan Enter dau tien (de bao cao) */
        }
        if (s_active != UART_PORT_NONE)          /* Nhan lenh tu moi cong */
        {
            s_rx_bytes++;
            (void)RingBuf_Push(&s_rx, byte);
        }
    }

    if (((base->CTRL & LPUART_CTRL_TIE_MASK) != 0u) &&
        ((stat & LPUART_STAT_TDRE_MASK) != 0u))
    {
        if (RingBuf_Pop(&s_tx[idx], &byte))
        {
            base->DATA = byte;
            s_tx_bytes[idx]++;
        }
        else
        {
            base->CTRL &= ~LPUART_CTRL_TIE_MASK;
        }
    }
}

void LPUART1_RxTx_IRQHandler(void) { Uart_IrqHandler(0u); }
void LPUART2_RxTx_IRQHandler(void) { Uart_IrqHandler(1u); }

/* ------------------------------------------------------------------ */
uint32_t Uart_WriteTo(uint32_t idx, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if ((data == NULL) || (idx >= UART_PORT_COUNT))
    {
        return 0u;
    }
    for (i = 0u; i < len; i++)
    {
        if (!RingBuf_Push(&s_tx[idx], data[i]))
        {
            s_tx[idx].dropped += (len - i - 1u);
            break;
        }
    }
    s_port[idx].base->CTRL |= LPUART_CTRL_TIE_MASK;
    return i;
}

/* Luon phat ra MOI cong: MaaZEDU co cong chi thong 1 chieu (J-Link VCOM chi TX),
   nen co the go lenh o cong nay va doc ket qua o cong kia. */
uint32_t Uart_Write(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t n = 0u;

    for (i = 0u; i < UART_PORT_COUNT; i++)
    {
        n = Uart_WriteTo(i, data, len);
    }
    return n;
}

uint32_t Uart_GetPortRx(uint32_t idx)
{
    return (idx < UART_PORT_COUNT) ? s_rx_raw[idx] : 0u;
}

uint32_t Uart_Puts(const char *s)
{
    return Uart_Write((const uint8_t *)s, (uint32_t)strlen(s));
}

static uint32_t FormatV(char *buf, uint32_t size, const char *fmt, va_list ap)
{
    int n = vsnprintf(buf, size, fmt, ap);

    if (n <= 0)
    {
        return 0u;
    }
    if ((uint32_t)n >= size)
    {
        n = (int)size - 1;
    }
    return (uint32_t)n;
}

uint32_t Uart_Printf(const char *fmt, ...)
{
    char     buf[UART_PRINTF_BUF];
    uint32_t n;
    va_list  ap;

    va_start(ap, fmt);
    n = FormatV(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return Uart_Write((const uint8_t *)buf, n);
}

uint32_t Uart_PrintfTo(uint32_t idx, const char *fmt, ...)
{
    char     buf[UART_PRINTF_BUF];
    uint32_t n;
    va_list  ap;

    va_start(ap, fmt);
    n = FormatV(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return Uart_WriteTo(idx, (const uint8_t *)buf, n);
}

bool Uart_ReadByte(uint8_t *data)
{
    return RingBuf_Pop(&s_rx, data);
}

uint32_t Uart_TxFree(void)
{
    uint32_t i;
    uint32_t f;
    uint32_t min_free = RingBuf_Free(&s_tx[0]);

    for (i = 1u; i < UART_PORT_COUNT; i++)
    {
        f = RingBuf_Free(&s_tx[i]);
        if (f < min_free)
        {
            min_free = f;
        }
    }
    return min_free;
}

bool Uart_IsLocked(void)
{
    return (s_active != UART_PORT_NONE);
}

const char *Uart_GetPortName(uint32_t idx)
{
    return (idx < UART_PORT_COUNT) ? s_port[idx].name : "?";
}

const char *Uart_GetActiveName(void)
{
    int32_t active = s_active;
    return (active == UART_PORT_NONE) ? "CHUA BAT DAU (go Enter o bat ky cong nao)"
                                      : "TAT CA (in ra moi cong, nhan lenh tu moi cong)";
}

void Uart_GetStats(uart_stats_t *st)
{
    int32_t  active = s_active;
    uint32_t idx = (active == UART_PORT_NONE) ? 0u : (uint32_t)active;

    st->rx_bytes    = s_rx_bytes;
    st->tx_bytes    = s_tx_bytes[idx];
    st->rx_dropped  = s_rx.dropped;
    st->tx_dropped  = s_tx[idx].dropped;
    st->err_overrun = s_err_or;
    st->err_framing = s_err_fe;
    st->err_noise   = s_err_nf;
    st->rx_peak     = s_rx.peak;
    st->tx_peak     = s_tx[idx].peak;
    st->baud_actual = s_baud_actual;
}

void Uart_ClearStats(void)
{
    uint32_t i;

    s_rx_bytes = 0u;
    s_err_or   = 0u;
    s_err_fe   = 0u;
    s_err_nf   = 0u;
    s_rx.dropped = 0u;
    s_rx.peak    = 0u;
    for (i = 0u; i < UART_PORT_COUNT; i++)
    {
        s_tx_bytes[i]  = 0u;
        s_tx[i].dropped = 0u;
        s_tx[i].peak    = 0u;
    }
}
