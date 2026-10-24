/*
 * adc.c - ADC0 S32K144, 12 bit, trigger mem, quet kenh bang ngat COCO
 *
 * Clock: PCC PCS = FIRCDIV2 (48 MHz), ADIV = /2 -> ADCK = 24 MHz (< 50 MHz max)
 * Thanh ghi dung:
 *   PCC->PCCn[PCC_ADC0_INDEX] : PCS + CGC
 *   ADC0->CFG1  : ADICLK, MODE (12 bit), ADIV
 *   ADC0->CFG2  : SMPLTS (thoi gian lay mau)
 *   ADC0->SC2   : ADTRG = 0 (trigger mem)
 *   ADC0->SC3   : CAL (tu hieu chuan), AVGE/AVGS (trung binh phan cung)
 *   ADC0->SC1[0]: ADCH (chon kenh, ghi = bat dau chuyen doi), AIEN, COCO
 *   ADC0->R[0]  : ket qua
 */
#include <stddef.h>
#include "S32K144.h"
#include "adc.h"
#include "nvic.h"
#include "../app_cfg.h"

#define ADC_ADICLK_ALT1     0u      /* Nguon clock tu PCC                  */
#define ADC_MODE_12BIT      1u
#define ADC_ADIV_DIV2       1u
#define ADC_SMPLTS_CYCLES   12u     /* 13 chu ky lay mau: du cho LM35      */
#define ADC_AVGS_16         2u      /* Trung binh 16 mau / ket qua         */
#define ADC_AVGS_32         3u      /* Dung khi hieu chuan                 */
#define ADC_PORT_MUX_ANALOG 0u

static uint8_t           s_ch[ADC_MAX_CHANNELS];
static uint32_t          s_count;
static volatile uint16_t s_raw[ADC_MAX_CHANNELS];
static volatile uint32_t s_idx;
static volatile bool     s_busy;
static volatile uint32_t s_overrun;
static adc_scan_cb_t     s_cb;

/* ------------------------------------------------------------------ */
static status_t Adc_Calibrate(void)
{
    uint32_t timeout = CFG_ADC_CAL_TIMEOUT;

    /* Quy trinh RM: xoa thanh ghi hieu chuan, trung binh 32 mau, set CAL */
    ADC0->CLPS = 0u;
    ADC0->CLP3 = 0u;
    ADC0->CLP2 = 0u;
    ADC0->CLP1 = 0u;
    ADC0->CLP0 = 0u;
    ADC0->CLPX = 0u;
    ADC0->CLP9 = 0u;

    ADC0->SC3 = ADC_SC3_AVGE_MASK | ADC_SC3_AVGS(ADC_AVGS_32) | ADC_SC3_CAL_MASK;
    while (((ADC0->SC3 & ADC_SC3_CAL_MASK) != 0u) && (timeout > 0u))
    {
        timeout--;                                 /* CAL tu xoa khi xong */
    }
    if (timeout == 0u)
    {
        return STATUS_ERR_TIMEOUT;
    }
    return STATUS_OK;
}

status_t Adc_Init(const uint8_t *channels, uint32_t count, adc_scan_cb_t cb)
{
    uint32_t i;
    status_t st;

    if ((channels == NULL) || (count == 0u) || (count > ADC_MAX_CHANNELS))
    {
        return STATUS_ERR_PARAM;
    }
    for (i = 0u; i < count; i++)
    {
        if (channels[i] > (ADC_SC1_ADCH_MASK >> ADC_SC1_ADCH_SHIFT))
        {
            return STATUS_ERR_PARAM;
        }
        s_ch[i] = channels[i];
    }
    s_count   = count;
    s_cb      = cb;
    s_busy    = false;
    s_overrun = 0u;

    /* 1. Chan analog (MUX = 0) */
    PCC->PCCn[PCC_PORTC_INDEX] |= PCC_PCCn_CGC_MASK;
    PORTC->PCR[CFG_ADC_PIN_LM35] = PORT_PCR_MUX(ADC_PORT_MUX_ANALOG);
    PORTC->PCR[CFG_ADC_PIN_POT]  = PORT_PCR_MUX(ADC_PORT_MUX_ANALOG);

    /* 2. Clock ADC0 (PCS ghi khi CGC = 0) */
    PCC->PCCn[PCC_ADC0_INDEX] = 0u;
    PCC->PCCn[PCC_ADC0_INDEX] = PCC_PCCn_PCS(CFG_PCC_PCS_FIRCDIV2);
    PCC->PCCn[PCC_ADC0_INDEX] |= PCC_PCCn_CGC_MASK;
    if ((PCC->PCCn[PCC_ADC0_INDEX] & PCC_PCCn_PR_MASK) == 0u)
    {
        return STATUS_ERR_HW;
    }

    /* 3. Cau hinh co ban */
    ADC0->SC1[0] = ADC_SC1_ADCH_MASK;              /* ADCH = 0x1F: tat module */
    ADC0->CFG1   = ADC_CFG1_ADICLK(ADC_ADICLK_ALT1) |
                   ADC_CFG1_MODE(ADC_MODE_12BIT)    |
                   ADC_CFG1_ADIV(ADC_ADIV_DIV2);
    ADC0->CFG2   = ADC_CFG2_SMPLTS(ADC_SMPLTS_CYCLES);
    ADC0->SC2    = 0u;                             /* Trigger mem, khong DMA, khong compare */

    /* 4. Tu hieu chuan */
    st = Adc_Calibrate();
    if (st != STATUS_OK)
    {
        return st;
    }

    /* 5. Trung binh phan cung 16 mau / ket qua */
    ADC0->SC3 = ADC_SC3_AVGE_MASK | ADC_SC3_AVGS(ADC_AVGS_16);

    Nvic_EnableIrq((uint32_t)ADC0_IRQn, CFG_IRQ_PRIO_ADC);
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
status_t Adc_StartScan(void)
{
    if (s_busy)
    {
        s_overrun++;                                /* Lan quet truoc chua xong */
        return STATUS_ERR_BUSY;
    }
    s_busy = true;
    s_idx  = 0u;
    ADC0->SC1[0] = ADC_SC1_AIEN_MASK | ADC_SC1_ADCH(s_ch[0]);   /* Ghi = bat dau */
    return STATUS_OK;
}

void ADC0_IRQHandler(void)
{
    uint32_t idx = s_idx;

    s_raw[idx] = (uint16_t)(ADC0->R[0] & ADC_R_D_MASK);   /* Doc R xoa COCO */
    idx++;

    if (idx < s_count)
    {
        s_idx = idx;
        ADC0->SC1[0] = ADC_SC1_AIEN_MASK | ADC_SC1_ADCH(s_ch[idx]);
    }
    else
    {
        s_busy = false;
        if (s_cb != NULL)
        {
            s_cb((const uint16_t *)s_raw, s_count);
        }
    }
}

bool Adc_IsBusy(void)
{
    return s_busy;
}

uint32_t Adc_GetOverrunCount(void)
{
    return s_overrun;
}
