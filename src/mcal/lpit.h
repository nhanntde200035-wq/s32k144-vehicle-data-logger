/*
 * lpit.h - LPIT0
 *   Kenh 0: tick he thong 1 ms (thay cho moi ham delay) + dong ho us
 *   Kenh 1: nhip lay mau dinh ky (goi callback trong ngat) + do jitter
 */
#ifndef LPIT_H
#define LPIT_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

typedef void (*sample_tick_cb_t)(uint32_t timestamp_us);

typedef struct
{
    uint32_t count;           /* So nhip da phat                     */
    uint32_t period_us;       /* Chu ky danh dinh                    */
    uint32_t min_us;          /* Chu ky thuc te nho nhat             */
    uint32_t max_us;          /* Chu ky thuc te lon nhat             */
} sample_timing_t;

status_t Tick_Init(void);
uint32_t Tick_GetMs(void);
uint32_t Tick_GetUs(void);    /* Do phan giai ~0,02 us, tran sau ~71 phut */

/* Kiem tra "da qua period_ms ke tu *last" - dung cho moi tac vu dinh ky
   trong super-loop, tu cap nhat *last khi den han. Dung ca khi tick tu tran. */
bool     Tick_Elapsed(uint32_t *last, uint32_t period_ms);

status_t SampleTimer_Init(uint32_t period_ms, sample_tick_cb_t cb);
void     SampleTimer_GetTiming(sample_timing_t *t);
void     SampleTimer_ResetTiming(void);

#endif /* LPIT_H */
