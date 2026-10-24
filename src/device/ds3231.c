/*
 * ds3231.c - Thanh ghi DS3231 (BCD):
 *   0x00 giay  0x01 phut  0x02 gio (bit6 = 12h)  0x03 thu  0x04 ngay
 *   0x05 thang (bit7 = the ky)  0x06 nam (00..99)
 *   0x0E control  0x0F status (bit7 OSF: dao dong tung dung -> gio khong tin cay)
 *   0x11/0x12 nhiet do (MSB co dau, 2 bit cao cua LSB = 0,25 °C)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ds3231.h"
#include "../mcal/lpi2c.h"
#include "../app_cfg.h"

#define REG_SECONDS     0x00u
#define REG_CONTROL     0x0Eu
#define REG_STATUS      0x0Fu
#define REG_TEMP_MSB    0x11u
#define TIME_REG_COUNT  7u
#define STATUS_OSF      0x80u
#define HOUR_12H_BIT    0x40u
#define MASK_SEC        0x7Fu
#define MASK_HOUR24     0x3Fu
#define MASK_MONTH      0x1Fu
#define MASK_DAY        0x3Fu
#define BASE_YEAR       2000u
#define TEMP_FRAC_SHIFT 6u
#define TEMP_QUARTER    25

#define SEC_PER_MIN     60u
#define SEC_PER_HOUR    3600u
#define SEC_PER_DAY     86400u
#define DAYS_1970_2000  10957u        /* So ngay tu 1970-01-01 den 2000-01-01 */

static const uint16_t s_days_before_month[12] =
    { 0u, 31u, 59u, 90u, 120u, 151u, 181u, 212u, 243u, 273u, 304u, 334u };

static uint8_t Bcd2Bin(uint8_t v) { return (uint8_t)(((v >> 4) * 10u) + (v & 0x0Fu)); }
static uint8_t Bin2Bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

static bool IsLeap(uint32_t y)
{
    return ((y % 4u) == 0u) && (((y % 100u) != 0u) || ((y % 400u) == 0u));
}

static uint8_t DaysInMonth(uint32_t y, uint32_t m)
{
    static const uint8_t dim[12] = { 31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u };
    return ((m == 2u) && IsLeap(y)) ? 29u : dim[m - 1u];
}

/* ------------------------------------------------------------------ */
status_t Ds3231_Init(bool *osc_stopped)
{
    uint8_t  sr = 0u;
    status_t st = I2c_ReadReg(CFG_DS3231_ADDR, REG_STATUS, &sr, 1u);

    if (st == STATUS_OK)
    {
        *osc_stopped = ((sr & STATUS_OSF) != 0u);
    }
    return st;
}

status_t Ds3231_GetTime(rtc_time_t *t)
{
    uint8_t  r[TIME_REG_COUNT];
    status_t st = I2c_ReadReg(CFG_DS3231_ADDR, REG_SECONDS, r, TIME_REG_COUNT);

    if (st != STATUS_OK)
    {
        return st;
    }
    t->sec   = Bcd2Bin(r[0] & MASK_SEC);
    t->min   = Bcd2Bin(r[1]);
    t->hour  = ((r[2] & HOUR_12H_BIT) != 0u) ? 0u : Bcd2Bin(r[2] & MASK_HOUR24);  /* Luon dat 24h */
    t->day   = Bcd2Bin(r[4] & MASK_DAY);
    t->month = Bcd2Bin(r[5] & MASK_MONTH);
    t->year  = (uint16_t)(BASE_YEAR + Bcd2Bin(r[6]));
    return Rtc_IsValid(t) ? STATUS_OK : STATUS_ERR_HW;
}

status_t Ds3231_SetTime(const rtc_time_t *t)
{
    uint8_t  r[TIME_REG_COUNT];
    uint8_t  sr = 0u;
    uint32_t days;
    status_t st;

    if (!Rtc_IsValid(t))
    {
        return STATUS_ERR_PARAM;
    }
    days = Rtc_ToEpoch(t) / SEC_PER_DAY;          /* 1970-01-01 la thu Nam (=5) */
    r[0] = Bin2Bcd(t->sec);
    r[1] = Bin2Bcd(t->min);
    r[2] = Bin2Bcd(t->hour);                      /* bit6 = 0: che do 24h */
    r[3] = (uint8_t)(((days + 4u) % 7u) + 1u);    /* 1 = Chu nhat */
    r[4] = Bin2Bcd(t->day);
    r[5] = Bin2Bcd(t->month);
    r[6] = Bin2Bcd((uint8_t)(t->year - BASE_YEAR));
    st = I2c_WriteReg(CFG_DS3231_ADDR, REG_SECONDS, r, TIME_REG_COUNT);
    if (st == STATUS_OK)
    {
        st = I2c_WriteReg(CFG_DS3231_ADDR, REG_STATUS, &sr, 1u);   /* Xoa OSF */
    }
    return st;
}

status_t Ds3231_GetTempX100(int32_t *temp_x100)
{
    uint8_t  r[2];
    status_t st = I2c_ReadReg(CFG_DS3231_ADDR, REG_TEMP_MSB, r, 2u);

    if (st == STATUS_OK)
    {
        *temp_x100 = ((int32_t)(int8_t)r[0] * 100) + ((int32_t)(r[1] >> TEMP_FRAC_SHIFT) * TEMP_QUARTER);
    }
    return st;
}

/* ------------------------------------------------------------------ */
bool Rtc_IsValid(const rtc_time_t *t)
{
    return (t->year >= BASE_YEAR) && (t->year <= (BASE_YEAR + 99u)) &&
           (t->month >= 1u) && (t->month <= 12u) &&
           (t->day >= 1u) && (t->day <= DaysInMonth(t->year, t->month)) &&
           (t->hour < 24u) && (t->min < 60u) && (t->sec < 60u);
}

uint32_t Rtc_ToEpoch(const rtc_time_t *t)
{
    uint32_t y = t->year;
    uint32_t days = DAYS_1970_2000;
    uint32_t i;

    for (i = BASE_YEAR; i < y; i++)
    {
        days += IsLeap(i) ? 366u : 365u;
    }
    days += s_days_before_month[t->month - 1u];
    if ((t->month > 2u) && IsLeap(y))
    {
        days++;
    }
    days += (uint32_t)t->day - 1u;
    return (days * SEC_PER_DAY) + ((uint32_t)t->hour * SEC_PER_HOUR) +
           ((uint32_t)t->min * SEC_PER_MIN) + t->sec;
}

void Rtc_FromEpoch(uint32_t epoch, rtc_time_t *t)
{
    uint32_t days = epoch / SEC_PER_DAY;
    uint32_t rem  = epoch % SEC_PER_DAY;
    uint32_t y    = BASE_YEAR;
    uint32_t m    = 1u;
    uint32_t ylen;

    t->hour = (uint8_t)(rem / SEC_PER_HOUR);
    t->min  = (uint8_t)((rem % SEC_PER_HOUR) / SEC_PER_MIN);
    t->sec  = (uint8_t)(rem % SEC_PER_MIN);

    days = (days >= DAYS_1970_2000) ? (days - DAYS_1970_2000) : 0u;
    for (;;)
    {
        ylen = IsLeap(y) ? 366u : 365u;
        if (days < ylen)
        {
            break;
        }
        days -= ylen;
        y++;
    }
    while (days >= DaysInMonth(y, m))
    {
        days -= DaysInMonth(y, m);
        m++;
    }
    t->year  = (uint16_t)y;
    t->month = (uint8_t)m;
    t->day   = (uint8_t)(days + 1u);
}

void Rtc_Format(const rtc_time_t *t, char *buf, uint32_t size)
{
    (void)snprintf(buf, size, "%04u-%02u-%02u %02u:%02u:%02u",
                   (unsigned)t->year, (unsigned)t->month, (unsigned)t->day,
                   (unsigned)t->hour, (unsigned)t->min, (unsigned)t->sec);
}

static bool Parse3(const char *s, char sep, uint32_t *a, uint32_t *b, uint32_t *c)
{
    char *end;

    if (s == NULL) { return false; }
    *a = (uint32_t)strtoul(s, &end, 10);
    if (*end != sep) { return false; }
    *b = (uint32_t)strtoul(end + 1, &end, 10);
    if (*end != sep) { return false; }
    *c = (uint32_t)strtoul(end + 1, &end, 10);
    return (*end == '\0');
}

bool Rtc_Parse(const char *date, const char *time, rtc_time_t *t)
{
    uint32_t y, mo, d, h, mi, s;

    if ((!Parse3(date, '-', &y, &mo, &d)) || (!Parse3(time, ':', &h, &mi, &s)))
    {
        return false;
    }
    if ((y <= 31u) && (d >= BASE_YEAR))       /* Nhap dang DD-MM-YYYY -> doi cho */
    {
        uint32_t tmp = y;
        y = d;
        d = tmp;
    }
    t->year  = (uint16_t)y;
    t->month = (uint8_t)mo;
    t->day   = (uint8_t)d;
    t->hour  = (uint8_t)h;
    t->min   = (uint8_t)mi;
    t->sec   = (uint8_t)s;
    return Rtc_IsValid(t);
}
