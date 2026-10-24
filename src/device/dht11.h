/*
 * dht11.h - Cam bien nhiet do + do am DHT11 (giao tiep 1 day, J8-11 = PTD13)
 *
 * Giao thuc:
 *   MCU keo DATA xuong 0 >= 18 ms -> tha (dien tro keo len tren module)
 *   DHT: 0 trong 80 us, 1 trong 80 us, roi 40 bit:
 *        moi bit = 0 trong 50 us + 1 trong 26-28 us (bit 0) hoac 70 us (bit 1)
 *   5 byte: do_am_nguyen, do_am_le, nhiet_nguyen, nhiet_le, checksum (tong 4 byte dau)
 *
 * Khong chan vong lap: pha keo xuong 20 ms chay theo trang thai (tick ms),
 * chi pha doc 40 bit (~4-5 ms) la cho bang vong lap. Khong tat ngat:
 * do do rong xung bang bo dem phan cung (Tick_GetUs) nen ngat chi lam xung
 * dai them vai us, nguong 0/1 la 48 us nen van dung; loi se bi checksum bat.
 */
#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

typedef struct
{
    bool     valid;          /* Da co it nhat 1 lan doc dung va chua qua cu */
    int16_t  temp_x10;       /* °C x10                                    */
    uint16_t hum_x10;        /* %RH x10                                   */
    uint32_t reads;          /* Tong so lan doc                           */
    uint32_t ok;
    uint32_t err_timeout;    /* Khong thay phan hoi / mat xung            */
    uint32_t err_checksum;
    uint32_t last_ok_ms;     /* Tick luc doc dung gan nhat                */
    uint32_t last_read_us;   /* Thoi gian pha doc 40 bit                  */
    uint8_t  last_err_stage; /* 0 = ok; 1..3 = buoc phan hoi; 4 = bit data */
} dht11_info_t;

status_t Dht11_Init(void);
void     Dht11_Task(void);                          /* Goi lien tuc trong super-loop */
bool     Dht11_Get(int16_t *temp_x10, uint16_t *hum_x10);   /* false neu chua co / qua cu */
void     Dht11_GetInfo(dht11_info_t *info);

#endif /* DHT11_H */
