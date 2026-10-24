/*
 * sensor.h - Lop dich vu cam bien: nhip 10 Hz -> quet ADC -> doi don vi + hieu chuan
 *
 * Kenh:
 *   SENSOR_LM35    : nhiet do (LM35, 10 mV/°C)
 *   SENSOR_POT     : bien tro (gia lap ban dap ga / cam bien vi tri)
 *   SENSOR_BANDGAP : bandgap noi -> tinh VDD thuc te
 *
 * Hieu chuan tuyen tinh tung kenh: mv_cal = mv_raw * gain / 10000 + offset_mv
 */
#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

typedef enum
{
    SENSOR_LM35 = 0,
    SENSOR_POT,
    SENSOR_BANDGAP,
    SENSOR_COUNT
} sensor_ch_t;

typedef struct
{
    uint32_t seq;                    /* So thu tu mau (tang dan)        */
    uint32_t timestamp_us;           /* Thoi diem kich lay mau          */
    uint16_t raw[SENSOR_COUNT];      /* Gia tri ADC 12 bit              */
    int32_t  mv[SENSOR_COUNT];       /* Dien ap sau hieu chuan (mV)     */
    uint32_t vref_mv;                /* VDD dung de doi raw -> mV       */
    int32_t  temp_c_x10;             /* Nhiet do LM35 x10 (vd 253 = 25,3 °C) */
    int32_t  pot_pct_x10;            /* Bien tro % x10                  */
} sensor_sample_t;

typedef struct
{
    int32_t gain;                    /* x10000 (10000 = 1,0)            */
    int32_t offset_mv;
} sensor_cal_t;

typedef struct
{
    uint32_t samples;                /* Tong so mau quet xong           */
    uint32_t missed;                 /* Nhip bi bo vi ADC con ban       */
    uint32_t vref_fallback;          /* So lan bandgap ngoai nguong     */
} sensor_stats_t;

status_t    Sensor_Init(void);
bool        Sensor_GetLatest(sensor_sample_t *out);    /* false neu chua co mau */
bool        Sensor_PopNew(sensor_sample_t *out);       /* true chi khi co mau moi chua doc */
status_t    Sensor_SetCal(sensor_ch_t ch, int32_t gain, int32_t offset_mv);
void        Sensor_GetCal(sensor_ch_t ch, sensor_cal_t *cal);
void        Sensor_SetVrefOverride(uint32_t vref_mv);  /* 0 = tu dong theo bandgap */
uint32_t    Sensor_GetVrefOverride(void);
void        Sensor_GetStats(sensor_stats_t *st);
const char *Sensor_GetName(sensor_ch_t ch);

#endif /* SENSOR_H */
