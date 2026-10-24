/*
 * clock.c - Cau hinh SCG muc thanh ghi
 *
 * Sau reset, S32K144 chay RUN mode bang FIRC 48 MHz (DIVCORE = 1).
 * Ta chi can:
 *   1. Xac nhan FIRC da on dinh (FIRCVLD).
 *   2. Bat FIRCDIV1/FIRCDIV2 = /1 -> 48 MHz cap cho LPUART, LPIT, LPSPI, LPI2C, ADC
 *      (mac dinh sau reset cac bo chia nay = 0 tuc la TAT -> ngoai vi khong co clock).
 *   3. Kiem tra core thuc su dang lay nguon tu FIRC.
 */
#include "S32K144.h"
#include "clock.h"
#include "../app_cfg.h"

#define SCG_SCS_FIRC        3u   /* SCG_CSR[SCS] = 3: FIRC lam clock he thong */
#define SCG_DIV_BY_1        1u   /* Gia tri truong FIRCDIVx: 1 = chia 1      */

status_t Clock_Init(void)
{
    uint32_t timeout = CFG_CLK_TIMEOUT;

    /* 1. Cho FIRC hop le (co timeout, khong treo vo han) */
    while (((SCG->FIRCCSR & SCG_FIRCCSR_FIRCVLD_MASK) == 0u) && (timeout > 0u))
    {
        timeout--;
    }
    if (timeout == 0u)
    {
        return STATUS_ERR_TIMEOUT;
    }

    /* 2. Bat clock bat dong bo cho ngoai vi */
    SCG->FIRCDIV = SCG_FIRCDIV_FIRCDIV1(SCG_DIV_BY_1) |
                   SCG_FIRCDIV_FIRCDIV2(SCG_DIV_BY_1);

    /* 3. Xac nhan nguon clock he thong */
    if (((SCG->CSR & SCG_CSR_SCS_MASK) >> SCG_CSR_SCS_SHIFT) != SCG_SCS_FIRC)
    {
        return STATUS_ERR_HW;
    }

    return STATUS_OK;
}
