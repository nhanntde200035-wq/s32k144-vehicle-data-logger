/*
 * ds3231.h - RTC DS3231 qua I2C (dia chi 0x68)
 */
#ifndef DS3231_H
#define DS3231_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

typedef struct
{
    uint16_t year;    /* 2000..2099 */
    uint8_t  month;   /* 1..12      */
    uint8_t  day;     /* 1..31      */
    uint8_t  hour;    /* 0..23      */
    uint8_t  min;
    uint8_t  sec;
} rtc_time_t;

status_t Ds3231_Init(bool *osc_stopped);           /* Kiem tra co mat ACK, co OSF */
status_t Ds3231_GetTime(rtc_time_t *t);
status_t Ds3231_SetTime(const rtc_time_t *t);      /* Xoa luon co OSF */
status_t Ds3231_GetTempX100(int32_t *temp_x100);   /* Cam bien nhiet noi, 0,25 °C */

bool     Rtc_IsValid(const rtc_time_t *t);
uint32_t Rtc_ToEpoch(const rtc_time_t *t);         /* Giay tu 1970-01-01 (UTC gia dinh) */
void     Rtc_FromEpoch(uint32_t epoch, rtc_time_t *t);
void     Rtc_Format(const rtc_time_t *t, char *buf, uint32_t size);  /* "YYYY-MM-DD HH:MM:SS" */
bool     Rtc_Parse(const char *date, const char *time, rtc_time_t *t);/* "YYYY-MM-DD" "HH:MM:SS" */

#endif /* DS3231_H */
