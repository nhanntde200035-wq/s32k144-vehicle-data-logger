/*
 * timesvc.c
 *   - Moi CFG_RTC_READ_PERIOD_MS doc DS3231 (7 byte, ~1 ms @100 kHz) -> moc epoch + moc tick.
 *   - NowEpoch() = moc_epoch + (tick hien tai - moc_tick) / 1000 -> khong can doc I2C moi lan.
 *   - Moi 10 lan doc thi doc them nhiet do chip DS3231 (de doi chieu voi LM35).
 */
#include "timesvc.h"
#include "../mcal/lpi2c.h"
#include "../mcal/lpit.h"
#include "../app_cfg.h"

#define MS_PER_S          1000u
#define TEMP_READ_EVERY   10u
#define ERR_REINIT_COUNT  3u        /* Loi lien tiep -> go ket bus + khoi tao lai I2C */

static timesvc_info_t s_info;
static uint32_t       s_base_epoch;
static uint32_t       s_base_ms;
static uint32_t       s_last_ms;
static uint32_t       s_err_run;

static void ReadNow(void)
{
    rtc_time_t t;
    uint32_t   t0 = Tick_GetUs();
    status_t   st = Ds3231_GetTime(&t);
    uint32_t   dt = Tick_GetUs() - t0;

    s_info.reads++;
    s_info.last_read_us = dt;
    if (dt > s_info.max_read_us)
    {
        s_info.max_read_us = dt;
    }
    if (st == STATUS_OK)
    {
        s_base_epoch = Rtc_ToEpoch(&t);
        s_base_ms    = Tick_GetMs();
        s_info.valid = !s_info.osc_stopped;
        s_err_run    = 0u;
    }
    else
    {
        s_info.read_errors++;
        if (++s_err_run >= ERR_REINIT_COUNT)
        {
            (void)I2c_Init(I2c_GetSpeedKhz());   /* Tu phuc hoi khi bus bi ket */
            s_err_run = 0u;
        }
    }
    if ((s_info.reads % TEMP_READ_EVERY) == 1u)
    {
        (void)Ds3231_GetTempX100(&s_info.rtc_temp_x100);
    }
}

status_t TimeSvc_Init(void)
{
    status_t st = I2c_Init(CFG_I2C_SPEED_KHZ);

    s_info.found = false;
    s_info.valid = false;
    if (st != STATUS_OK)
    {
        return st;
    }
    st = Ds3231_Init(&s_info.osc_stopped);
    if (st != STATUS_OK)
    {
        (void)I2c_Init(CFG_I2C_SPEED_KHZ);         /* Thu lai sau khi go ket bus */
        st = Ds3231_Init(&s_info.osc_stopped);
    }
    if (st != STATUS_OK)
    {
        return st;                                  /* Khong thay DS3231 */
    }
    s_info.found = true;
    ReadNow();
    s_last_ms = Tick_GetMs();
    return STATUS_OK;
}

void TimeSvc_Task(void)
{
    if (s_info.found && Tick_Elapsed(&s_last_ms, CFG_RTC_READ_PERIOD_MS))
    {
        ReadNow();
    }
}

uint32_t TimeSvc_NowEpoch(void)
{
    if (!s_info.valid)
    {
        return 0u;
    }
    return s_base_epoch + ((Tick_GetMs() - s_base_ms) / MS_PER_S);
}

bool TimeSvc_Now(rtc_time_t *t)
{
    uint32_t e = TimeSvc_NowEpoch();

    if (e == 0u)
    {
        return false;
    }
    Rtc_FromEpoch(e, t);
    return true;
}

status_t TimeSvc_Set(const rtc_time_t *t)
{
    status_t st;

    if (!s_info.found)
    {
        return STATUS_ERR_HW;
    }
    st = Ds3231_SetTime(t);
    if (st != STATUS_OK)
    {
        (void)I2c_Init(I2c_GetSpeedKhz());       /* Go ket bus roi thu lai 1 lan */
        st = Ds3231_SetTime(t);
    }
    if (st == STATUS_OK)
    {
        s_info.osc_stopped = false;
        ReadNow();
    }
    return st;
}

void TimeSvc_GetInfo(timesvc_info_t *info)
{
    *info = s_info;
}
