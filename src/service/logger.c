/*
 * logger.c - Xem mo ta dinh dang ban ghi trong logger.h
 *
 * Nguyen tac:
 *   - Moi truy cap Flash chi xay ra trong Logger_Task() (super-loop), KHONG trong ngat.
 *   - Khong bao gio cho Flash: neu W25Q dang BUSY thi Logger_Task() tra ve ngay,
 *     lan goi sau lam tiep. Ban ghi moi duoc xep vao hang doi RAM trong luc cho.
 *   - Vung log: [0, size - 64 KB). 64 KB cuoi danh cho FLASH TEST.
 *   - Khoi dong: tim nhi phan o trong dau tien (magic = 0xFFFF) -> vi tri ghi tiep.
 *     O nao magic trong nhung con byte khac 0xFF (ghi do khi mat dien) -> bo qua.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "logger.h"
#include "sensor.h"
#include "timesvc.h"
#include "../device/w25q.h"
#include "../device/dht11.h"
#include "../mcal/lpuart.h"
#include "../mcal/lpit.h"
#include "../app_cfg.h"

#define REC_MAGIC           0xA55Au
#define REC_EMPTY16         0xFFFFu
#define REC_FLAG_MISSED     0x01u
#define DHT_NONE            ((int16_t)0x7FFF)
#define HUM_NONE            0xFFu
#define CRC16_INIT          0xFFFFu
#define CRC16_POLY          0x1021u
#define CRC16_TOPBIT        0x8000u
#define BITS_PER_BYTE       8u
#define SKIP_SCAN_MAX       16u       /* So o ghi do toi da bo qua khi khoi dong */
#define LAST_VALID_SCAN     16u       /* Lui toi da de tim ban ghi hop le cuoi  */
#define WRITE_RETRY_MAX     3u
#define DUMP_LINES_PER_CALL 4u
#define X10_DIV             10
#define NUM_BUF             12u

typedef struct
{
    uint16_t magic;
    uint16_t session;
    uint32_t seq;
    uint32_t time_s;
    uint32_t uptime_ms;
    uint16_t raw_lm35;
    uint16_t raw_pot;
    int16_t  dht_temp_x10;    /* DHT11 °C x10, DHT_NONE neu khong co */
    int16_t  temp_x10;
    int16_t  pot_x10;
    uint16_t vdd_mv;
    uint8_t  flags;
    uint8_t  hum_pct;         /* DHT11 %RH, HUM_NONE neu khong co   */
    uint16_t crc16;
} log_record_t;

/* Kich thuoc phai dung 32 byte (khong padding) */
typedef char rec_size_check[(sizeof(log_record_t) == LOG_RECORD_SIZE) ? 1 : -1];

/* ---------------- Trang thai ---------------- */
static logger_state_t s_state = LOGGER_NO_FLASH;
static uint32_t       s_capacity;
static uint32_t       s_next;            /* O ghi tiep theo */
static uint16_t       s_session;
static uint32_t       s_seq_next;
static uint32_t       s_written;
static uint32_t       s_dropped;
static uint32_t       s_write_err;
static uint32_t       s_write_retry;
static uint32_t       s_skipped;
static uint32_t       s_last_missed;

static log_record_t   s_queue[CFG_LOG_QUEUE_LEN];
static uint32_t       s_q_head;
static uint32_t       s_q_tail;
static uint32_t       s_q_peak;

static bool           s_erase_all;
static bool           s_erase_issued;
static uint32_t       s_erase_done;
static uint32_t       s_erase_total;

static uint32_t       s_dump_idx;
static uint32_t       s_dump_end;
static uint32_t       s_dump_start;
static uint32_t       s_dump_crc_err;

/* ------------------------------------------------------------------ */
static uint16_t Crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = CRC16_INIT;
    uint32_t i;
    uint32_t b;

    for (i = 0u; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << BITS_PER_BYTE);
        for (b = 0u; b < BITS_PER_BYTE; b++)
        {
            crc = ((crc & CRC16_TOPBIT) != 0u) ? (uint16_t)((crc << 1) ^ CRC16_POLY)
                                               : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool RecordCrcOk(const log_record_t *r)
{
    return (r->magic == REC_MAGIC) &&
           (Crc16((const uint8_t *)r, LOG_RECORD_SIZE - sizeof(uint16_t)) == r->crc16);
}

static uint32_t SlotAddr(uint32_t slot)
{
    return slot * LOG_RECORD_SIZE;
}

static bool ReadSlot(uint32_t slot, log_record_t *r)
{
    return (W25q_Read(SlotAddr(slot), (uint8_t *)r, LOG_RECORD_SIZE) == STATUS_OK);
}

static bool SlotIsBlank(uint32_t slot)
{
    log_record_t r;
    const uint8_t *p = (const uint8_t *)&r;
    uint32_t i;

    if (!ReadSlot(slot, &r))
    {
        return false;
    }
    for (i = 0u; i < LOG_RECORD_SIZE; i++)
    {
        if (p[i] != 0xFFu)
        {
            return false;
        }
    }
    return true;
}

static uint32_t QueueCount(void)
{
    return s_q_head - s_q_tail;
}

/* ------------------------------------------------------------------ */
status_t Logger_Init(void)
{
    uint32_t     size = W25q_GetSize();
    uint32_t     lo;
    uint32_t     hi;
    uint32_t     mid;
    uint32_t     i;
    log_record_t r;

    s_q_head = 0u;
    s_q_tail = 0u;
    s_session = 0u;
    s_seq_next = 0u;
    s_skipped = 0u;

    if (size <= CFG_LOG_TEST_AREA_BYTES)
    {
        s_state = LOGGER_NO_FLASH;
        return STATUS_ERR_HW;
    }
    s_capacity = (size - CFG_LOG_TEST_AREA_BYTES) / LOG_RECORD_SIZE;

    /* 1. Tim nhi phan o dau tien co magic = 0xFFFF */
    lo = 0u;
    hi = s_capacity;
    while (lo < hi)
    {
        mid = lo + ((hi - lo) / 2u);
        if (!ReadSlot(mid, &r))
        {
            s_state = LOGGER_NO_FLASH;
            return STATUS_ERR_HW;
        }
        if (r.magic == REC_EMPTY16)
        {
            hi = mid;
        }
        else
        {
            lo = mid + 1u;
        }
    }
    s_next = lo;

    /* 2. Bo qua o bi ghi do (magic con FF nhung byte khac da bi lap trinh) */
    for (i = 0u; (i < SKIP_SCAN_MAX) && (s_next < s_capacity) && (!SlotIsBlank(s_next)); i++)
    {
        s_next++;
        s_skipped++;
    }

    /* 3. Lay session / seq tu ban ghi hop le cuoi cung */
    for (i = 0u; (i < LAST_VALID_SCAN) && (i < s_next); i++)
    {
        if (ReadSlot(s_next - 1u - i, &r) && RecordCrcOk(&r))
        {
            s_session  = r.session;
            s_seq_next = r.seq + 1u;
            break;
        }
    }

    s_state = (s_next >= s_capacity) ? LOGGER_FULL : LOGGER_IDLE;
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
status_t Logger_Start(void)
{
    sensor_sample_t m;
    sensor_stats_t  ss;

    if (s_state != LOGGER_IDLE)
    {
        return STATUS_ERR_BUSY;
    }
    while (Sensor_PopNew(&m))
    {
        /* Bo mau cu: phien moi chi ghi mau lay sau lenh START */
    }
    Sensor_GetStats(&ss);
    s_last_missed = ss.missed;
    s_session++;
    s_written     = 0u;
    s_dropped     = 0u;
    s_write_err   = 0u;
    s_q_peak      = 0u;
    s_state       = LOGGER_RUNNING;
    return STATUS_OK;
}

status_t Logger_Stop(void)
{
    if (s_state != LOGGER_RUNNING)
    {
        return STATUS_ERR_BUSY;
    }
    s_state = LOGGER_IDLE;            /* Hang doi van duoc ghi not trong Logger_Task */
    return STATUS_OK;
}

status_t Logger_Erase(bool all)
{
    if ((s_state != LOGGER_IDLE) && (s_state != LOGGER_FULL))
    {
        return STATUS_ERR_BUSY;
    }
    if (QueueCount() != 0u)
    {
        return STATUS_ERR_BUSY;
    }
    s_erase_all    = all;
    s_erase_issued = false;
    s_erase_done   = 0u;
    s_erase_total  = all ? 1u
                         : ((SlotAddr(s_next) + CFG_FLASH_SECTOR_SIZE - 1u) / CFG_FLASH_SECTOR_SIZE);
    s_state        = LOGGER_ERASING;
    return STATUS_OK;
}

status_t Logger_Dump(uint32_t last_n)
{
    if (((s_state != LOGGER_IDLE) && (s_state != LOGGER_FULL)) || (QueueCount() != 0u))
    {
        return STATUS_ERR_BUSY;
    }
    s_dump_end     = s_next;
    s_dump_idx     = ((last_n == 0u) || (last_n >= s_next)) ? 0u : (s_next - last_n);
    s_dump_start   = s_dump_idx;
    s_dump_crc_err = 0u;
    (void)Uart_Printf("OK DUMP %lu records\r\n", (unsigned long)(s_dump_end - s_dump_idx));
    (void)Uart_Puts("slot,seq,session,datetime,uptime_ms,temp_C,pot_pct,vdd_mV,"
                    "raw_lm35,raw_pot,dht_temp_C,hum_pct,flags,crc\r\n");
    s_state = LOGGER_DUMPING;
    return STATUS_OK;
}

void Logger_Abort(void)
{
    if ((s_state == LOGGER_DUMPING) || ((s_state == LOGGER_ERASING) && (!s_erase_all)))
    {
        (void)Uart_Printf("\r\nABORT %s\r\n> ", (s_state == LOGGER_DUMPING) ? "DUMP" : "ERASE");
        s_state = (s_next >= s_capacity) ? LOGGER_FULL : LOGGER_IDLE;
    }
}

/* ---------------------- Cac buoc trong Task ----------------------- */
static void CollectSamples(void)
{
    sensor_sample_t m;
    sensor_stats_t  ss;
    log_record_t   *r;
    while (Sensor_PopNew(&m))
    {
        if (QueueCount() >= CFG_LOG_QUEUE_LEN)
        {
            s_dropped++;
            continue;
        }
        Sensor_GetStats(&ss);
        r = &s_queue[s_q_head % CFG_LOG_QUEUE_LEN];
        r->magic     = REC_MAGIC;
        r->session   = s_session;
        r->seq       = s_seq_next++;
        r->time_s    = TimeSvc_NowEpoch();          /* 0 neu chua co RTC hop le */
        r->uptime_ms = Tick_GetMs();                /* us cua mau tran sau 71 phut -> dung ms */
        r->raw_lm35 = m.raw[SENSOR_LM35];
        r->raw_pot  = m.raw[SENSOR_POT];
        {
            int16_t  dt;
            uint16_t dh;

            if (Dht11_Get(&dt, &dh))
            {
                r->dht_temp_x10 = dt;
                r->hum_pct      = (uint8_t)((dh + 5u) / 10u);
            }
            else
            {
                r->dht_temp_x10 = DHT_NONE;
                r->hum_pct      = HUM_NONE;
            }
        }
        r->temp_x10 = (int16_t)m.temp_c_x10;
        r->pot_x10  = (int16_t)m.pot_pct_x10;
        r->vdd_mv   = (uint16_t)m.vref_mv;
        r->flags    = (ss.missed != s_last_missed) ? REC_FLAG_MISSED : 0u;
        s_last_missed = ss.missed;
        r->crc16    = Crc16((const uint8_t *)r, LOG_RECORD_SIZE - sizeof(uint16_t));
        s_q_head++;
        if (QueueCount() > s_q_peak)
        {
            s_q_peak = QueueCount();
        }
    }
}

static void WriteNext(void)
{
    status_t st;

    if (s_next >= s_capacity)
    {
        s_dropped += QueueCount();
        s_q_tail   = s_q_head;
        s_state    = LOGGER_FULL;
        (void)Uart_Puts("\r\nWARN LOG FULL - go ERASE\r\n");
        return;
    }
    st = W25q_ProgramStart(SlotAddr(s_next), (const uint8_t *)&s_queue[s_q_tail % CFG_LOG_QUEUE_LEN],
                           LOG_RECORD_SIZE);
    if (st == STATUS_OK)
    {
        s_next++;
        s_written++;
        s_q_tail++;
        s_write_retry = 0u;
    }
    else if (st != STATUS_ERR_BUSY)
    {
        s_write_err++;
        if (++s_write_retry >= WRITE_RETRY_MAX)
        {
            s_next++;                 /* Bo o loi, ghi sang o ke tiep */
            s_write_retry = 0u;
        }
    }
    else
    {
        /* Flash ban: lan sau thu lai */
    }
}

static void EraseStep(void)
{
    status_t st;

    if (s_erase_all)
    {
        if (!s_erase_issued)
        {
            st = W25q_EraseChipStart();
            if (st == STATUS_OK)
            {
                s_erase_issued = true;
            }
            return;
        }
        s_erase_done = 1u;            /* Toi day nghia la chip da het BUSY */
    }
    else if (s_erase_done < s_erase_total)
    {
        st = W25q_EraseSectorStart(s_erase_done * CFG_FLASH_SECTOR_SIZE);
        if (st == STATUS_OK)
        {
            s_erase_done++;
        }
        return;
    }
    else
    {
        /* Da ra lenh xoa sector cuoi va da het BUSY */
    }

    s_next     = 0u;
    s_seq_next = 0u;
    s_skipped  = 0u;
    s_state    = LOGGER_IDLE;
    (void)Uart_Printf("\r\nOK ERASE done (%s)\r\n> ", s_erase_all ? "ALL" : "USED");
}

static const char *FmtX10(char *buf, int32_t v)
{
    uint32_t a = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
    char    *p = buf + NUM_BUF - 1u;

    *p = '\0';
    *--p = (char)('0' + (a % (uint32_t)X10_DIV));
    *--p = '.';
    a /= (uint32_t)X10_DIV;
    do
    {
        *--p = (char)('0' + (a % (uint32_t)X10_DIV));
        a /= (uint32_t)X10_DIV;
    } while (a != 0u);
    if (v < 0)
    {
        *--p = '-';
    }
    return p;
}

static void DumpStep(void)
{
    log_record_t r;
    rtc_time_t   t;
    char         dt[24];
    char         b1[NUM_BUF];
    char         b2[NUM_BUF];
    char         b3[NUM_BUF];
    char         b4[NUM_BUF];
    uint32_t     n;
    bool         ok;

    for (n = 0u; (n < DUMP_LINES_PER_CALL) && (s_dump_idx < s_dump_end); n++)
    {
        if (Uart_TxFree() < CFG_LOG_DUMP_LINE_MAX)
        {
            return;                   /* Cho UART day bot du lieu, khong chan */
        }
        if (!ReadSlot(s_dump_idx, &r))
        {
            return;
        }
        ok = RecordCrcOk(&r);
        if (!ok)
        {
            s_dump_crc_err++;
        }
        if (r.time_s != 0u)
        {
            Rtc_FromEpoch(r.time_s, &t);
            Rtc_Format(&t, dt, sizeof(dt));
        }
        else
        {
            (void)strcpy(dt, "-");
        }
        if (r.dht_temp_x10 == DHT_NONE) { (void)strcpy(b3, "-"); } else { (void)FmtX10(b3, r.dht_temp_x10); }
        if (r.hum_pct == HUM_NONE)      { (void)strcpy(b4, "-"); }
        else                            { (void)snprintf(b4, sizeof(b4), "%u", (unsigned)r.hum_pct); }
        (void)Uart_Printf("%lu,%lu,%u,%s,%lu,%s,%s,%u,%u,%u,%s,%s,%u,%s\r\n",
                          (unsigned long)s_dump_idx, (unsigned long)r.seq, (unsigned)r.session,
                          dt, (unsigned long)r.uptime_ms,
                          FmtX10(b1, r.temp_x10), FmtX10(b2, r.pot_x10), (unsigned)r.vdd_mv,
                          (unsigned)r.raw_lm35, (unsigned)r.raw_pot,
                          b3, b4, (unsigned)r.flags,
                          ok ? "OK" : "BAD");
        s_dump_idx++;
    }
    if (s_dump_idx >= s_dump_end)
    {
        (void)Uart_Printf("OK DUMP END n=%lu crc_err=%lu\r\n> ",
                          (unsigned long)(s_dump_end - s_dump_start), (unsigned long)s_dump_crc_err);
        s_state = (s_next >= s_capacity) ? LOGGER_FULL : LOGGER_IDLE;
    }
}

void Logger_Task(void)
{
    if (s_state == LOGGER_NO_FLASH)
    {
        return;
    }
    if (s_state == LOGGER_RUNNING)
    {
        CollectSamples();
    }
    if (W25q_IsBusy())
    {
        return;                       /* Flash dang ghi/xoa: quay lai sau */
    }
    if (s_state == LOGGER_ERASING)
    {
        EraseStep();
    }
    else if (QueueCount() != 0u)
    {
        WriteNext();
    }
    else if (s_state == LOGGER_DUMPING)
    {
        DumpStep();
    }
    else
    {
        /* Khong co viec */
    }
}

/* ------------------------------------------------------------------ */
void Logger_GetInfo(logger_info_t *info)
{
    info->state         = s_state;
    info->capacity      = s_capacity;
    info->count         = s_next;
    info->session       = s_session;
    info->written       = s_written;
    info->queue_peak    = s_q_peak;
    info->dropped       = s_dropped;
    info->write_errors  = s_write_err;
    info->skipped_slots = s_skipped;
    info->erase_done    = s_erase_done;
    info->erase_total   = s_erase_total;
    info->dump_done     = s_dump_idx - s_dump_start;
    info->dump_crc_err  = s_dump_crc_err;
}

logger_state_t Logger_GetState(void)
{
    return s_state;
}

const char *Logger_StateName(logger_state_t st)
{
    static const char * const names[] =
        { "NO_FLASH", "IDLE", "RUNNING", "ERASING", "DUMPING", "FULL" };

    return ((uint32_t)st < (sizeof(names) / sizeof(names[0]))) ? names[st] : "?";
}

bool Logger_IsFlashBusyByLogger(void)
{
    return (s_state == LOGGER_RUNNING) || (s_state == LOGGER_ERASING) ||
           (s_state == LOGGER_DUMPING) || (QueueCount() != 0u);
}
