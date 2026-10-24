/*
 * display.c
 *   - Moi CFG_DISPLAY_PERIOD_MS: dung lai noi dung tung dong (chuoi co do dai co dinh).
 *   - Dong nao khac lan ve truoc moi bi danh dau "ban" (dirty).
 *   - Moi lan Display_Task() chi ve 1 dong ban (~10 ms @ 8 MHz) -> khong chan vong lap lau.
 */
#include <stdio.h>
#include <string.h>
#include "display.h"
#include "timesvc.h"
#include "sensor.h"
#include "logger.h"
#include "../device/st7735.h"
#include "../device/dht11.h"
#include "../mcal/lpit.h"
#include "../app_cfg.h"

#define LINE_COUNT     8u
#define LINE_LEN       22u      /* 21 ky tu (scale 1) + '\0' */
#define BIG_COLS       10u      /* 128 / 12 */
#define SMALL_COLS     21u      /* 128 / 6  */
#define X10_DIV        10

typedef struct
{
    uint16_t y;
    uint8_t  scale;
    uint16_t fg;
} line_cfg_t;

static const line_cfg_t s_cfg[LINE_COUNT] =
{
    {   2u, 1u, TFT_CYAN   },   /* Tieu de            */
    {  14u, 2u, TFT_WHITE  },   /* Ngay               */
    {  34u, 2u, TFT_YELLOW },   /* Gio                */
    {  56u, 2u, TFT_GREEN  },   /* REC / IDLE         */
    {  78u, 2u, TFT_ORANGE },   /* Nhiet do           */
    {  98u, 2u, TFT_CYAN   },   /* Bien tro           */
    { 118u, 2u, TFT_WHITE  },   /* DHT11 nhiet + am   */
    { 138u, 2u, TFT_GRAY   }    /* So ban ghi         */
};

static char     s_shown[LINE_COUNT][LINE_LEN];
static char     s_want[LINE_COUNT][LINE_LEN];
static uint16_t s_color[LINE_COUNT];
static uint32_t s_last_ms;
static bool     s_ok;

/* Chuoi do dai co dinh: them khoang trang de xoa ky tu cu */
static void SetLine(uint32_t i, const char *txt, uint16_t color)
{
    uint32_t cols = (s_cfg[i].scale == 1u) ? SMALL_COLS : BIG_COLS;
    uint32_t n = (uint32_t)strlen(txt);
    uint32_t k;

    if (n > cols) { n = cols; }
    for (k = 0u; k < cols; k++)
    {
        s_want[i][k] = (k < n) ? txt[k] : ' ';
    }
    s_want[i][cols] = '\0';
    s_color[i] = color;
}

static void FmtX10(char *buf, uint32_t size, const char *prefix, int32_t v, const char *unit)
{
    int32_t a = (v < 0) ? -v : v;
    (void)snprintf(buf, size, "%s%s%ld.%ld%s", prefix, (v < 0) ? "-" : "",
                   (long)(a / X10_DIV), (long)(a % X10_DIV), unit);
}

static void Build(void)
{
    char            b[LINE_LEN + 8u];
    rtc_time_t      t;
    sensor_sample_t m;
    logger_info_t   li;

    SetLine(0u, "P2-03 VEHICLE LOGGER", TFT_CYAN);

    if (TimeSvc_Now(&t))
    {
        (void)snprintf(b, sizeof(b), "%04u-%02u-%02u", (unsigned)t.year, (unsigned)t.month, (unsigned)t.day);
        SetLine(1u, b, TFT_WHITE);
        (void)snprintf(b, sizeof(b), "%02u:%02u:%02u", (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec);
        SetLine(2u, b, TFT_YELLOW);
    }
    else
    {
        SetLine(1u, "NO RTC", TFT_RED);
        (void)snprintf(b, sizeof(b), "UP %lus", (unsigned long)(Tick_GetMs() / 1000u));
        SetLine(2u, b, TFT_YELLOW);
    }

    Logger_GetInfo(&li);
    if (li.state == LOGGER_RUNNING)
    {
        (void)snprintf(b, sizeof(b), "REC S%u", (unsigned)li.session);
        SetLine(3u, b, TFT_RED);
    }
    else
    {
        SetLine(3u, Logger_StateName(li.state), TFT_GREEN);
    }

    if (Sensor_GetLatest(&m))
    {
        FmtX10(b, sizeof(b), "T ", m.temp_c_x10, "C");
        SetLine(4u, b, TFT_ORANGE);
        FmtX10(b, sizeof(b), "P ", m.pot_pct_x10, "%");
        SetLine(5u, b, TFT_CYAN);
    }
    {
        int16_t  dt;
        uint16_t dh;

        if (Dht11_Get(&dt, &dh))
        {
            (void)snprintf(b, sizeof(b), "D%d.%dC %u%%", (int)(dt / X10_DIV),
                           (int)(((dt < 0) ? -dt : dt) % X10_DIV), (unsigned)((dh + 5u) / 10u));
            SetLine(6u, b, TFT_WHITE);
        }
        else
        {
            SetLine(6u, "DHT --", TFT_GRAY);
        }
    }
    (void)snprintf(b, sizeof(b), "N %lu", (unsigned long)li.count);
    SetLine(7u, b, TFT_GRAY);
}

/* ------------------------------------------------------------------ */
status_t Display_Init(void)
{
    uint32_t i;

    s_ok = Tft_IsReady();
    for (i = 0u; i < LINE_COUNT; i++)
    {
        s_shown[i][0] = '\0';
        s_want[i][0]  = '\0';
    }
    s_last_ms = Tick_GetMs();
    if (s_ok)
    {
        Build();
    }
    return s_ok ? STATUS_OK : STATUS_ERR_HW;
}

void Display_Task(void)
{
    uint32_t i;

    if (!s_ok)
    {
        return;
    }
    if (Tick_Elapsed(&s_last_ms, CFG_DISPLAY_PERIOD_MS))
    {
        Build();
    }
    for (i = 0u; i < LINE_COUNT; i++)
    {
        if (strcmp(s_shown[i], s_want[i]) != 0)
        {
            Tft_DrawString(0u, s_cfg[i].y, s_want[i], s_color[i], TFT_BLACK, s_cfg[i].scale);
            (void)strcpy(s_shown[i], s_want[i]);
            return;                                /* Toi da 1 dong / lan goi */
        }
    }
}
