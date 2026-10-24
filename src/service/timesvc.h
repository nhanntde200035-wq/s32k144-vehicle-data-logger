/*
 * timesvc.h - Dich vu thoi gian: doc DS3231 moi 1 s, noi suy giua 2 lan doc bang tick.
 */
#ifndef TIMESVC_H
#define TIMESVC_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"
#include "../device/ds3231.h"

typedef struct
{
    bool     found;            /* DS3231 tra loi ACK            */
    bool     valid;            /* Gio hop le (da dat, khong OSF) */
    bool     osc_stopped;      /* OSF: pin het / chua dat gio   */
    int32_t  rtc_temp_x100;    /* Nhiet do chip DS3231          */
    uint32_t reads;
    uint32_t read_errors;
    uint32_t last_read_us;     /* Thoi gian 1 lan doc I2C       */
    uint32_t max_read_us;
} timesvc_info_t;

status_t TimeSvc_Init(void);
void     TimeSvc_Task(void);
uint32_t TimeSvc_NowEpoch(void);                 /* 0 neu chua co gio hop le */
bool     TimeSvc_Now(rtc_time_t *t);
status_t TimeSvc_Set(const rtc_time_t *t);
void     TimeSvc_GetInfo(timesvc_info_t *info);

#endif /* TIMESVC_H */
