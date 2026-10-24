/*
 * nvic.h - Cau hinh NVIC muc thanh ghi (S32K144: 4 bit uu tien)
 */
#ifndef NVIC_H
#define NVIC_H

#include <stdint.h>
#include "S32K144.h"

#define NVIC_PRIO_BITS_SHIFT   4u   /* S32K144 dung 4 bit cao cua IP[n] */

static inline void Nvic_EnableIrq(uint32_t irq, uint32_t prio)
{
    S32_NVIC->ICPR[irq >> 5u] = (1u << (irq & 31u));   /* Xoa pending cu */
    S32_NVIC->IP[irq]         = (uint8_t)(prio << NVIC_PRIO_BITS_SHIFT);
    S32_NVIC->ISER[irq >> 5u] = (1u << (irq & 31u));   /* Cho phep ngat  */
}

static inline void Nvic_DisableIrq(uint32_t irq)
{
    S32_NVIC->ICER[irq >> 5u] = (1u << (irq & 31u));
}

static inline void Irq_EnableGlobal(void)  { __asm volatile ("cpsie i" ::: "memory"); }
static inline void Irq_DisableGlobal(void) { __asm volatile ("cpsid i" ::: "memory"); }

#endif /* NVIC_H */
