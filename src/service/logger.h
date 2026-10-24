/*
 * logger.h - Ghi ban ghi cam bien vao Flash SPI (khong chan), DUMP CSV, ERASE,
 *            tu phuc hoi vi tri ghi sau mat dien.
 *
 * Ban ghi 32 byte (8 ban ghi / page 256 byte, khong bao gio vuot ranh gioi page):
 *   off  size  truong
 *    0    2    magic      = 0xA55A (0xFFFF = o trong)
 *    2    2    session    phien ghi (tang moi lan START)
 *    4    4    seq        so thu tu toan cuc
 *    8    4    time_s     epoch (giay tu 1970) tu RTC DS3231 - 0 neu chua co
 *   12    4    uptime_ms  thoi diem lay mau tinh tu luc bat nguon
 *   16    2    raw_lm35   ADC LM35
 *   18    2    raw_pot    ADC bien tro
 *   20    2    dht_temp   DHT11 °C x10 (int16, 0x7FFF = khong co)
 *   22    2    temp_x10   nhiet do x10 (int16)
 *   24    2    pot_x10    % x10 (int16)
 *   26    2    vdd_mv
 *   28    1    flags      bit0: co mau ADC bi bo truoc ban ghi nay
 *   29    1    hum_pct    DHT11 %RH (0xFF = khong co)
 *   30    2    crc16      CRC-16/CCITT-FALSE tren 30 byte dau
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

#define LOG_RECORD_SIZE   32u

typedef enum
{
    LOGGER_NO_FLASH = 0,    /* Khong co Flash                       */
    LOGGER_IDLE,
    LOGGER_RUNNING,
    LOGGER_ERASING,
    LOGGER_DUMPING,
    LOGGER_FULL
} logger_state_t;

typedef struct
{
    logger_state_t state;
    uint32_t capacity;         /* So ban ghi toi da                */
    uint32_t count;            /* So ban ghi dang co trong Flash   */
    uint16_t session;          /* Phien hien tai / cuoi cung       */
    uint32_t written;          /* Ban ghi da ghi trong phien nay   */
    uint32_t queue_peak;
    uint32_t dropped;          /* Mat do hang doi day              */
    uint32_t write_errors;
    uint32_t skipped_slots;    /* O bi ghi do (mat dien) bi bo qua */
    uint32_t erase_done;       /* Tien do ERASE: sector da xoa     */
    uint32_t erase_total;
    uint32_t dump_done;        /* Tien do DUMP                     */
    uint32_t dump_crc_err;
} logger_info_t;

status_t       Logger_Init(void);              /* Quet Flash tim vi tri ghi */
void           Logger_Task(void);              /* Goi lien tuc trong super-loop */
status_t       Logger_Start(void);
status_t       Logger_Stop(void);
status_t       Logger_Erase(bool all);         /* all=false: chi xoa phan da dung */
status_t       Logger_Dump(uint32_t last_n);   /* 0 = tat ca */
void           Logger_Abort(void);             /* Dung DUMP / ERASE */
void           Logger_GetInfo(logger_info_t *info);
logger_state_t Logger_GetState(void);
const char    *Logger_StateName(logger_state_t st);
bool           Logger_IsFlashBusyByLogger(void);

#endif /* LOGGER_H */
