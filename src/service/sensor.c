/*
 * sensor.c
 *
 * Luong du lieu:
 *   LPIT kenh 1 (10 Hz, ngat) -> Adc_StartScan()
 *   ADC0 ngat COCO x3          -> OnScanDone(): chep raw + timestamp vao snapshot
 *   main loop                  -> Sensor_PopNew(): doc snapshot, doi don vi, hieu chuan
 *
 * Snapshot dung "seqlock": ISR tang s_seq SAU khi ghi xong; ben doc chep ra roi
 * kiem tra s_seq khong doi -> khong can tat ngat, khong bi doc du lieu nua voi.
 */
#include <stddef.h>
#include "sensor.h"
#include "../mcal/adc.h"
#include "../mcal/lpit.h"
#include "../app_cfg.h"

#define SEQLOCK_RETRY_MAX   4u
#define PCT_X10_FULL        1000

static const uint8_t s_adc_ch[SENSOR_COUNT] =
{
    CFG_ADC_CH_LM35,
    CFG_ADC_CH_POT,
    CFG_ADC_CH_BANDGAP
};

static const char * const s_name[SENSOR_COUNT] = { "LM35", "POT", "VBG" };

/* ---- Snapshot ghi trong ngat ---- */
static volatile uint16_t s_snap_raw[SENSOR_COUNT];
static volatile uint32_t s_snap_ts_us;
static volatile uint32_t s_snap_seq;        /* 0 = chua co mau */
static volatile uint32_t s_pending_ts_us;   /* Thoi diem kich cua lan quet dang chay */

/* ---- Trang thai phia main ---- */
static uint32_t     s_read_seq;
static sensor_cal_t s_cal[SENSOR_COUNT];
static uint32_t     s_vref_override;
static uint32_t     s_vref_fallback;

/* ------------------------- Ngu canh ngat --------------------------- */
static void OnSampleTick(uint32_t timestamp_us)
{
    if (Adc_StartScan() == STATUS_OK)
    {
        s_pending_ts_us = timestamp_us;
    }
}

static void OnScanDone(const uint16_t *raw, uint32_t count)
{
    uint32_t i;

    for (i = 0u; (i < count) && (i < SENSOR_COUNT); i++)
    {
        s_snap_raw[i] = raw[i];
    }
    s_snap_ts_us = s_pending_ts_us;
    s_snap_seq++;                             /* Cong bo SAU khi ghi xong */
}

/* ------------------------------------------------------------------ */
status_t Sensor_Init(void)
{
    status_t st;
    uint32_t i;

    for (i = 0u; i < SENSOR_COUNT; i++)
    {
        s_cal[i].gain      = CFG_CAL_GAIN_ONE;
        s_cal[i].offset_mv = 0;
    }
    s_cal[SENSOR_LM35].offset_mv = CFG_LM35_OFFSET_MV;   /* Van chinh lai duoc bang lenh CAL */
    s_vref_override = 0u;
    s_vref_fallback = 0u;
    s_read_seq      = 0u;
    s_snap_seq      = 0u;

    st = Adc_Init(s_adc_ch, SENSOR_COUNT, OnScanDone);
    if (st != STATUS_OK)
    {
        return st;
    }
    return SampleTimer_Init(CFG_SAMPLE_PERIOD_MS, OnSampleTick);
}

/* Chep snapshot an toan (seqlock). Tra ve seq cua ban chep, 0 neu that bai */
static uint32_t ReadSnapshot(uint16_t *raw, uint32_t *ts_us)
{
    uint32_t seq_before;
    uint32_t seq_after;
    uint32_t retry;
    uint32_t i;

    for (retry = 0u; retry < SEQLOCK_RETRY_MAX; retry++)
    {
        seq_before = s_snap_seq;
        for (i = 0u; i < SENSOR_COUNT; i++)
        {
            raw[i] = s_snap_raw[i];
        }
        *ts_us = s_snap_ts_us;
        seq_after = s_snap_seq;
        if (seq_before == seq_after)
        {
            return seq_after;
        }
    }
    return 0u;
}

static uint32_t ComputeVref(uint16_t raw_bg)
{
    uint32_t vref;

    if (s_vref_override != 0u)
    {
        return s_vref_override;
    }
    if (raw_bg == 0u)
    {
        s_vref_fallback++;
        return CFG_ADC_VREF_DEFAULT_MV;
    }
    /* raw_bg = VBG / VREF * 4095  ->  VREF = VBG * 4095 / raw_bg */
    vref = (CFG_ADC_BANDGAP_MV * CFG_ADC_FULL_SCALE + (raw_bg / 2u)) / raw_bg;
    if ((vref < CFG_ADC_VREF_MIN_MV) || (vref > CFG_ADC_VREF_MAX_MV))
    {
        s_vref_fallback++;
        return CFG_ADC_VREF_DEFAULT_MV;
    }
    return vref;
}

static void Convert(uint32_t seq, const uint16_t *raw, uint32_t ts_us, sensor_sample_t *out)
{
    uint32_t i;
    int32_t  mv;

    out->seq          = seq;
    out->timestamp_us = ts_us;
    out->vref_mv      = ComputeVref(raw[SENSOR_BANDGAP]);

    for (i = 0u; i < SENSOR_COUNT; i++)
    {
        out->raw[i] = raw[i];
        mv = (int32_t)(((uint32_t)raw[i] * out->vref_mv + (CFG_ADC_FULL_SCALE / 2u)) /
                       CFG_ADC_FULL_SCALE);
        out->mv[i] = ((mv * s_cal[i].gain) / CFG_CAL_GAIN_ONE) + s_cal[i].offset_mv;
    }

    /* LM35: 10 mV/°C -> °C x10 = mV */
    out->temp_c_x10  = (out->mv[SENSOR_LM35] * 10) / CFG_LM35_MV_PER_DEGC;
    out->pot_pct_x10 = (out->mv[SENSOR_POT] * PCT_X10_FULL) / (int32_t)out->vref_mv;
}

bool Sensor_GetLatest(sensor_sample_t *out)
{
    uint16_t raw[SENSOR_COUNT];
    uint32_t ts_us;
    uint32_t seq = ReadSnapshot(raw, &ts_us);

    if (seq == 0u)
    {
        return false;
    }
    Convert(seq, raw, ts_us, out);
    return true;
}

bool Sensor_PopNew(sensor_sample_t *out)
{
    if (s_snap_seq == s_read_seq)
    {
        return false;
    }
    if (!Sensor_GetLatest(out))
    {
        return false;
    }
    s_read_seq = out->seq;
    return true;
}

status_t Sensor_SetCal(sensor_ch_t ch, int32_t gain, int32_t offset_mv)
{
    if ((ch >= SENSOR_COUNT) || (gain <= 0))
    {
        return STATUS_ERR_PARAM;
    }
    s_cal[ch].gain      = gain;
    s_cal[ch].offset_mv = offset_mv;
    return STATUS_OK;
}

void Sensor_GetCal(sensor_ch_t ch, sensor_cal_t *cal)
{
    if (ch < SENSOR_COUNT)
    {
        *cal = s_cal[ch];
    }
}

void Sensor_SetVrefOverride(uint32_t vref_mv)
{
    s_vref_override = vref_mv;
}

uint32_t Sensor_GetVrefOverride(void)
{
    return s_vref_override;
}

void Sensor_GetStats(sensor_stats_t *st)
{
    st->samples       = s_snap_seq;
    st->missed        = Adc_GetOverrunCount();
    st->vref_fallback = s_vref_fallback;
}

const char *Sensor_GetName(sensor_ch_t ch)
{
    return (ch < SENSOR_COUNT) ? s_name[ch] : "?";
}
