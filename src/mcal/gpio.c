/*
 * gpio.c - Thanh ghi dung: PCC (CGC), PORTD->PCR (MUX = 1: GPIO),
 *          PTD->PDDR (huong), PSOR / PCOR / PTOR (set / clear / toggle)
 */
#include "S32K144.h"
#include "gpio.h"
#include "../app_cfg.h"

#define PORT_MUX_GPIO   1u
#define LED_MASK        (1u << CFG_LED_PIN)

status_t Led_Init(void)
{
    PCC->PCCn[PCC_PORTD_INDEX] = PCC_PCCn_CGC_MASK;
    if ((PCC->PCCn[PCC_PORTD_INDEX] & PCC_PCCn_CGC_MASK) == 0u)
    {
        return STATUS_ERR_HW;
    }
    PORTD->PCR[CFG_LED_PIN] = PORT_PCR_MUX(PORT_MUX_GPIO);
    PTD->PSOR  = LED_MASK;      /* Muc cao = tat (tich cuc thap) */
    PTD->PDDR |= LED_MASK;      /* Ngo ra */
    return STATUS_OK;
}

void Led_On(void)     { PTD->PCOR = LED_MASK; }
void Led_Off(void)    { PTD->PSOR = LED_MASK; }
void Led_Toggle(void) { PTD->PTOR = LED_MASK; }
