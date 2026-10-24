/*
 * main.c - PEB301 P2-03 Vehicle Data Logger
 * Giai doan 1: Clock + Tick LPIT 1 ms + LED + UART console (ring buffer, ngat)
 * Giai doan 2: ADC 3 kenh lay mau 10 Hz (LPIT kenh 1 + ngat ADC) + hieu chuan
 * Giai doan 3: LPSPI0 + Flash W25Qxx (tu do chan, JEDEC ID, erase/program/read)
 * Giai doan 4: Logger ghi Flash khong chan, DUMP CSV, ERASE, chiu mat dien
 * Giai doan 5: LPI2C0 + RTC DS3231 (timestamp that) + TFT ST7735 (man hinh trang thai)
 *
 * Kien truc: super-loop khong chan. Moi dinh thoi deu dua tren Tick (LPIT),
 * khong co ham delay nao trong luong chinh.
 */
#include <stdint.h>
#include "../app_cfg.h"
#include "../status.h"
#include "../mcal/nvic.h"
#include "../mcal/clock.h"
#include "../mcal/lpit.h"
#include "../mcal/gpio.h"
#include "../mcal/lpuart.h"
#include "../service/sensor.h"
#include "../device/w25q.h"
#include "../service/logger.h"
#include "../service/timesvc.h"
#include "../service/display.h"
#include "../device/st7735.h"
#include "../device/dht11.h"
#include "console.h"

/* Ma loi khoi tao -> so lan LED nhay khi loi (khi UART chua dung duoc) */
typedef enum
{
    INIT_STAGE_CLOCK = 1,
    INIT_STAGE_LED,
    INIT_STAGE_TICK,
    INIT_STAGE_UART,
    INIT_STAGE_SENSOR,
    INIT_STAGE_CONSOLE
} init_stage_t;

static void FatalError(init_stage_t stage, status_t st)
{
    /* Chua co tick -> chi co the dung o day. LED sang lien tuc bao loi.
       Dung debugger xem 'stage' va 'st' de biet khoi nao hong. */
    volatile init_stage_t dbg_stage  = stage;
    volatile status_t     dbg_status = st;
    (void)dbg_stage;
    (void)dbg_status;

    Led_On();
    for (;;)
    {
        /* Dung han - khong chay tiep voi phan cung loi */
    }
}

int main(void)
{
    status_t st;
    uint32_t led_last = 0u;

    Irq_DisableGlobal();

    st = Clock_Init();
    if (st != STATUS_OK) { FatalError(INIT_STAGE_CLOCK, st); }

    st = Led_Init();
    if (st != STATUS_OK) { FatalError(INIT_STAGE_LED, st); }

    st = Tick_Init();
    if (st != STATUS_OK) { FatalError(INIT_STAGE_TICK, st); }

    st = Uart_Init(CFG_UART_BAUD);
    if (st != STATUS_OK) { FatalError(INIT_STAGE_UART, st); }

    st = Sensor_Init();                   /* ADC + nhip lay mau 10 Hz */
    if (st != STATUS_OK) { FatalError(INIT_STAGE_SENSOR, st); }

    Irq_EnableGlobal();

    {
        /* Flash: chua noi / loi -> van chay tiep, console bao loi va cho phep FLASH PROBE lai */
        w25q_probe_t probe;
        if (W25q_Probe(&probe) == STATUS_OK)
        {
            (void)Logger_Init();          /* Quet Flash tim vi tri ghi tiep (chiu mat dien) */
        }
    }

    (void)TimeSvc_Init();                 /* Khong co DS3231 -> van chay, time_s = 0 */
    (void)Tft_Init();                     /* SPI da khoi tao boi W25q_Probe */
    (void)Display_Init();
    (void)Dht11_Init();                   /* J8-11 PTD13 */

    st = Console_Init();
    if (st != STATUS_OK) { FatalError(INIT_STAGE_CONSOLE, st); }

    for (;;)
    {
        Console_Poll();
        Logger_Task();
        TimeSvc_Task();
        Display_Task();
        Dht11_Task();
        Console_LogTask();

        if (Tick_Elapsed(&led_last, CFG_LED_PERIOD_MS))
        {
            Led_Toggle();                 /* Nhip tim: he thong con song */
        }
    }
}
