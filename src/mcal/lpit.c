/*
 * lpit.c - LPIT0, clock FIRCDIV2 = 48 MHz, 32-bit periodic counter
 *
 *   Kenh 0 (tick 1 ms)   : TVAL = 48 000 000 / 1000 - 1 = 47 999
 *   Kenh 1 (lay mau 10 Hz): TVAL = 48 000 000 / 10   - 1 = 4 799 999
 *
 * Thanh ghi dung: PCC (PCS + CGC), LPIT0->MCR (M_CEN), TMR[n].TVAL/CVAL,
 *                 TMR[n].TCTRL (T_EN, MODE = 0), MIER (TIEn), MSR (TIFn - W1C)
 */
#include <stddef.h>
#include "S32K144.h"
#include "lpit.h"
#include "nvic.h"
#include "../app_cfg.h"

#define LPIT_CH_TICK      0u
#define LPIT_CH_SAMPLE    1u
#define CLK_PER_US        (CFG_PERIPH_CLK_HZ / 1000000u)
#define US_PER_MS         1000u
#define MS_PER_S          1000u

static volatile uint32_t s_ms;
static uint32_t          s_tick_tval;

static sample_tick_cb_t  s_sample_cb;
static volatile uint32_t s_last_us;
static volatile sample_timing_t s_timing;

/* ------------------------------------------------------------------ */
status_t Tick_Init(void)
{
    volatile uint32_t wait = CFG_LPIT_EN_DELAY;

    s_tick_tval = (CFG_PERIPH_CLK_HZ / CFG_TICK_HZ) - 1u;

    /* 1. Clock */
    PCC->PCCn[PCC_LPIT_INDEX] = 0u;
    PCC->PCCn[PCC_LPIT_INDEX] = PCC_PCCn_PCS(CFG_PCC_PCS_FIRCDIV2);
    PCC->PCCn[PCC_LPIT_INDEX] |= PCC_PCCn_CGC_MASK;
    if ((PCC->PCCn[PCC_LPIT_INDEX] & PCC_PCCn_PR_MASK) == 0u)
    {
        return STATUS_ERR_HW;
    }

    /* 2. Bat module; RM yeu cau cho >= 4 chu ky clock ngoai vi truoc khi
          truy cap thanh ghi khac (chi xay ra 1 lan luc khoi tao) */
    LPIT0->MCR = LPIT_MCR_M_CEN_MASK | LPIT_MCR_DBG_EN_MASK;  /* Chay ca khi debug dung */
    while (wait > 0u)
    {
        wait--;
    }

    /* 3. Kenh 0: periodic 32-bit */
    LPIT0->TMR[LPIT_CH_TICK].TCTRL = 0u;
    LPIT0->TMR[LPIT_CH_TICK].TVAL  = s_tick_tval;
    LPIT0->MSR  = LPIT_MSR_TIF0_MASK;
    LPIT0->MIER |= LPIT_MIER_TIE0_MASK;

    Nvic_EnableIrq((uint32_t)LPIT0_Ch0_IRQn, CFG_IRQ_PRIO_TICK);
    LPIT0->TMR[LPIT_CH_TICK].TCTRL = LPIT_TMR_TCTRL_T_EN_MASK;

    if (LPIT0->TMR[LPIT_CH_TICK].TVAL != s_tick_tval)
    {
        return STATUS_ERR_HW;
    }
    return STATUS_OK;
}

void LPIT0_Ch0_IRQHandler(void)
{
    LPIT0->MSR = LPIT_MSR_TIF0_MASK;              /* W1C */
    s_ms++;
}

uint32_t Tick_GetMs(void)
{
    return s_ms;
}

/* Thoi gian us = ms * 1000 + phan da dem trong ms hien tai.
   Bo dem LPIT dem LUI tu TVAL ve 0. Neu co TIF0 dang treo (vd. dang o trong
   ngat uu tien cao hon) va bo dem vua nap lai -> cong them 1 ms. */
uint32_t Tick_GetUs(void)
{
    uint32_t ms;
    uint32_t cval;
    uint32_t pending;

    /* Doc lai neu ngat tick xay ra giua 2 lan doc (tranh thoi gian bi lui 1 ms) */
    do
    {
        ms      = s_ms;
        cval    = LPIT0->TMR[LPIT_CH_TICK].CVAL;
        pending = LPIT0->MSR & LPIT_MSR_TIF0_MASK;
    } while (ms != s_ms);

    if ((pending != 0u) && (cval > (s_tick_tval / 2u)))
    {
        ms++;
    }
    return (ms * US_PER_MS) + ((s_tick_tval - cval) / CLK_PER_US);
}

bool Tick_Elapsed(uint32_t *last, uint32_t period_ms)
{
    uint32_t now = s_ms;

    if ((now - *last) >= period_ms)
    {
        *last += period_ms;                        /* Giu nhip, khong troi */
        if ((now - *last) >= period_ms)
        {
            *last = now;                           /* Bi tre qua nhieu -> dong bo lai */
        }
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
void SampleTimer_ResetTiming(void)
{
    s_timing.count  = 0u;
    s_timing.min_us = UINT32_MAX;
    s_timing.max_us = 0u;
}

status_t SampleTimer_Init(uint32_t period_ms, sample_tick_cb_t cb)
{
    uint32_t tval;

    if ((period_ms == 0u) || (period_ms > (UINT32_MAX / CFG_PERIPH_CLK_HZ) * MS_PER_S))
    {
        return STATUS_ERR_PARAM;
    }
    tval = ((CFG_PERIPH_CLK_HZ / MS_PER_S) * period_ms) - 1u;

    s_sample_cb       = cb;
    s_timing.period_us = period_ms * US_PER_MS;
    SampleTimer_ResetTiming();
    s_last_us = 0u;

    LPIT0->TMR[LPIT_CH_SAMPLE].TCTRL = 0u;
    LPIT0->TMR[LPIT_CH_SAMPLE].TVAL  = tval;
    LPIT0->MSR  = LPIT_MSR_TIF1_MASK;
    LPIT0->MIER |= LPIT_MIER_TIE1_MASK;

    Nvic_EnableIrq((uint32_t)LPIT0_Ch1_IRQn, CFG_IRQ_PRIO_SAMPLE);
    LPIT0->TMR[LPIT_CH_SAMPLE].TCTRL = LPIT_TMR_TCTRL_T_EN_MASK;

    if (LPIT0->TMR[LPIT_CH_SAMPLE].TVAL != tval)
    {
        return STATUS_ERR_HW;
    }
    return STATUS_OK;
}

void LPIT0_Ch1_IRQHandler(void)
{
    uint32_t now_us;
    uint32_t dt;

    LPIT0->MSR = LPIT_MSR_TIF1_MASK;              /* W1C */
    now_us = Tick_GetUs();

    if (s_timing.count > 0u)                      /* Bo qua nhip dau (chua co moc) */
    {
        dt = now_us - s_last_us;
        if (dt < s_timing.min_us) { s_timing.min_us = dt; }
        if (dt > s_timing.max_us) { s_timing.max_us = dt; }
    }
    s_last_us = now_us;
    s_timing.count++;

    if (s_sample_cb != NULL)
    {
        s_sample_cb(now_us);
    }
}

void SampleTimer_GetTiming(sample_timing_t *t)
{
    t->count     = s_timing.count;
    t->period_us = s_timing.period_us;
    t->min_us    = s_timing.min_us;
    t->max_us    = s_timing.max_us;
}
