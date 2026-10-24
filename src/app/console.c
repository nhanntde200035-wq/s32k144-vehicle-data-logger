/*
 * console.c
 */
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "console.h"
#include "../mcal/lpuart.h"
#include "../mcal/lpit.h"
#include "../service/sensor.h"
#include "../device/w25q.h"
#include "../mcal/lpspi.h"
#include "../service/logger.h"
#include "../service/timesvc.h"
#include "../mcal/lpi2c.h"
#include "../device/dht11.h"
#include "../app_cfg.h"

/* ---------- Ma loi console ---------- */
typedef enum
{
    CERR_UNKNOWN_CMD = 1,
    CERR_BAD_ARG     = 2,
    CERR_BAD_STATE   = 3,
    CERR_LINE_LONG   = 4,
    CERR_TIMEOUT     = 5,
    CERR_NO_STORAGE  = 10
} console_err_t;

typedef enum
{
    CMODE_LINE = 0,     /* Nhan dong lenh                 */
    CMODE_TEST          /* Dang nhan n byte tho cho TEST  */
} console_mode_t;

#define CRC32_INIT        0xFFFFFFFFu
#define CRC32_POLY_REFL   0xEDB88320u    /* Giong zlib.crc32 cua Python */
#define CRC32_XOR_OUT     0xFFFFFFFFu
#define BITS_PER_BYTE     8u
#define TEST_TIMEOUT_MS   3000u
#define ASCII_BACKSPACE   0x08u
#define ASCII_DEL         0x7Fu
#define ALIVE_PERIOD_MS   2000u
#define ASCII_PRINT_MIN   0x20u
#define ASCII_PRINT_MAX   0x7Eu
#define CONSOLE_PROMPT    "> "
#define X10_DIV           10
#define X10_BUF           12u
#define FLASH_DUMP_MAX    64u
#define FLASH_DUMP_COLS   16u
#define TEST_PATTERN_MUL  31u     /* Mau du lieu: (dia chi * 31 + seed) & 0xFF */

/* ---------- Trang thai ---------- */
static char           s_line[CFG_CMD_LINE_MAX];
static uint32_t       s_line_len;
static bool           s_line_overflow;
static console_mode_t s_mode;

static bool           s_running;
static bool           s_suppress_prompt;
static bool           s_log_on;
static uint32_t       s_log_period;
static uint32_t       s_log_last;
static uint32_t       s_session_start;
static uint32_t       s_alive_last;

static uint32_t       s_test_total;
static uint32_t       s_test_got;
static uint32_t       s_test_crc;
static uint32_t       s_test_last_rx;
static uint32_t       s_test_t0;

static void PrintLoggerStatus(void);
static void PrintRtcLine(const char *tag);
static void PrintDhtLine(void);

/* ------------------------------------------------------------------ */
static uint32_t Crc32_Update(uint32_t crc, uint8_t data)
{
    uint32_t i;

    crc ^= data;
    for (i = 0u; i < BITS_PER_BYTE; i++)
    {
        crc = ((crc & 1u) != 0u) ? ((crc >> 1) ^ CRC32_POLY_REFL) : (crc >> 1);
    }
    return crc;
}

static void ReplyErr(console_err_t code, const char *msg)
{
    (void)Uart_Printf("ERR %u %s\r\n", (unsigned)code, msg);
}

static bool ParseU32(const char *s, uint32_t *out)
{
    char         *end;
    unsigned long v;

    if ((s == NULL) || (*s == '\0'))
    {
        return false;
    }
    v = strtoul(s, &end, 10);
    if (*end != '\0')
    {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool ParseI32(const char *s, int32_t *out)
{
    char *end;
    long  v;

    if ((s == NULL) || (*s == '\0'))
    {
        return false;
    }
    v = strtol(s, &end, 10);
    if (*end != '\0')
    {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

/* In so x10 dang "-12.3" (khong dung printf float -> nhe, khong can newlib float) */
static const char *FmtX10(char *buf, int32_t v)
{
    uint32_t a = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
    char    *p = buf + X10_BUF - 1u;

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

static bool ParseSensor(const char *s, sensor_ch_t *ch)
{
    uint32_t i;

    for (i = 0u; i < (uint32_t)SENSOR_COUNT; i++)
    {
        if ((s != NULL) && (strcmp(s, Sensor_GetName((sensor_ch_t)i)) == 0))
        {
            *ch = (sensor_ch_t)i;
            return true;
        }
    }
    return false;
}

/* 1 dong du lieu cam bien: dung chung cho LOG va lenh ADC */
static void PrintSample(const char *tag, const sensor_sample_t *m, uint32_t t_ms)
{
    char b1[X10_BUF];
    char b2[X10_BUF];

    (void)Uart_Printf("%s t=%lu seq=%lu T=%s C pot=%s %% vdd=%lu mV\r\n",
                      tag, (unsigned long)t_ms, (unsigned long)m->seq,
                      FmtX10(b1, m->temp_c_x10), FmtX10(b2, m->pot_pct_x10),
                      (unsigned long)m->vref_mv);
}

/* ------------------------- FLASH ---------------------------------- */
static void PrintProbe(uint32_t port, bool to_port)
{
    w25q_probe_t p;

    W25q_GetLastProbe(&p);
    if (p.found)
    {
        if (to_port)
        {
            (void)Uart_PrintfTo(port, "  flash %s %s %lu KB jedec=%02X %02X %02X pincfg=%u sck=%lu Hz\r\n",
                                W25q_GetVendor(), W25q_GetName(), (unsigned long)(p.id.size_bytes / 1024u),
                                p.id.manuf, p.id.type, p.id.capacity, (unsigned)p.pincfg,
                                (unsigned long)Spi_GetSpeed());
        }
        else
        {
            (void)Uart_Printf("OK FLASH %s %s %lu KB jedec=%02X %02X %02X pincfg=%u sck=%lu Hz\r\n",
                              W25q_GetVendor(), W25q_GetName(), (unsigned long)(p.id.size_bytes / 1024u),
                              p.id.manuf, p.id.type, p.id.capacity, (unsigned)p.pincfg,
                              (unsigned long)Spi_GetSpeed());
        }
    }
    else
    {
        if (to_port)
        {
            (void)Uart_PrintfTo(port, "  flash KHONG THAY: jedec=%02X %02X %02X (FF=chan MISO ho, 00=chua cap nguon/CS)\r\n",
                                p.id.manuf, p.id.type, p.id.capacity);
        }
        else
        {
            (void)Uart_Printf("ERR 11 FLASH_NOT_FOUND jedec=%02X %02X %02X\r\n",
                              p.id.manuf, p.id.type, p.id.capacity);
        }
    }
}

static uint8_t PatternByte(uint32_t addr, uint32_t seed)
{
    return (uint8_t)((addr * TEST_PATTERN_MUL) + seed);
}

/* Xoa 1 sector -> ghi 16 page mau -> doc lai so sanh. Do thoi gian tung buoc */
static void Cmd_FlashTest(const char *arg)
{
    static uint8_t buf[CFG_FLASH_PAGE_SIZE];
    uint32_t sector = 0u;
    uint32_t base;
    uint32_t seed;
    uint32_t t0;
    uint32_t t_erase;
    uint32_t t_prog;
    uint32_t t_read;
    uint32_t errors = 0u;
    uint32_t page;
    uint32_t i;
    status_t st;

    if ((arg != NULL) && (!ParseU32(arg, &sector)))
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: FLASH TEST [sector]");
        return;
    }
    /* Chi test trong 64 KB cuoi (16 sector) de khong pha du lieu log */
    if ((W25q_GetSize() <= CFG_LOG_TEST_AREA_BYTES) ||
        (sector >= (CFG_LOG_TEST_AREA_BYTES / CFG_FLASH_SECTOR_SIZE)))
    {
        ReplyErr(CERR_BAD_STATE, "FLASH_NOT_READY_OR_SECTOR>15");
        return;
    }
    if (Logger_IsFlashBusyByLogger())
    {
        ReplyErr(CERR_BAD_STATE, "LOGGER_BUSY (STOP truoc)");
        return;
    }
    base = (W25q_GetSize() - CFG_LOG_TEST_AREA_BYTES) + (sector * CFG_FLASH_SECTOR_SIZE);
    seed = Tick_GetMs();                         /* Moi lan test mot mau khac */

    t0 = Tick_GetUs();
    st = W25q_EraseSector(base);
    t_erase = Tick_GetUs() - t0;
    if (st != STATUS_OK)
    {
        (void)Uart_Printf("ERR 12 ERASE_FAIL st=%u\r\n", (unsigned)st);
        return;
    }

    t0 = Tick_GetUs();
    for (page = 0u; page < (CFG_FLASH_SECTOR_SIZE / CFG_FLASH_PAGE_SIZE); page++)
    {
        for (i = 0u; i < CFG_FLASH_PAGE_SIZE; i++)
        {
            buf[i] = PatternByte(base + (page * CFG_FLASH_PAGE_SIZE) + i, seed);
        }
        st = W25q_ProgramPage(base + (page * CFG_FLASH_PAGE_SIZE), buf, CFG_FLASH_PAGE_SIZE);
        if (st != STATUS_OK)
        {
            (void)Uart_Printf("ERR 13 PROGRAM_FAIL page=%lu st=%u\r\n", (unsigned long)page, (unsigned)st);
            return;
        }
    }
    t_prog = Tick_GetUs() - t0;

    t0 = Tick_GetUs();
    for (page = 0u; page < (CFG_FLASH_SECTOR_SIZE / CFG_FLASH_PAGE_SIZE); page++)
    {
        st = W25q_Read(base + (page * CFG_FLASH_PAGE_SIZE), buf, CFG_FLASH_PAGE_SIZE);
        if (st != STATUS_OK)
        {
            (void)Uart_Printf("ERR 14 READ_FAIL page=%lu st=%u\r\n", (unsigned long)page, (unsigned)st);
            return;
        }
        for (i = 0u; i < CFG_FLASH_PAGE_SIZE; i++)
        {
            if (buf[i] != PatternByte(base + (page * CFG_FLASH_PAGE_SIZE) + i, seed))
            {
                errors++;
            }
        }
    }
    t_read = Tick_GetUs() - t0;

    (void)Uart_Printf("%s FLASH TEST sector=%lu bytes=%u errors=%lu\r\n",
                      (errors == 0u) ? "OK" : "ERR 15", (unsigned long)sector,
                      (unsigned)CFG_FLASH_SECTOR_SIZE, (unsigned long)errors);
    (void)Uart_Printf("  erase=%lu us program=%lu us (%lu us/page) read=%lu us sck=%lu Hz\r\n",
                      (unsigned long)t_erase, (unsigned long)t_prog,
                      (unsigned long)(t_prog / (CFG_FLASH_SECTOR_SIZE / CFG_FLASH_PAGE_SIZE)),
                      (unsigned long)t_read, (unsigned long)Spi_GetSpeed());
}

static void Cmd_FlashRead(const char *addr_s, const char *len_s)
{
    uint8_t  buf[FLASH_DUMP_MAX];
    uint32_t addr;
    uint32_t len = FLASH_DUMP_COLS;
    uint32_t i;

    if ((!ParseU32(addr_s, &addr)) || ((len_s != NULL) && (!ParseU32(len_s, &len))) ||
        (len == 0u) || (len > FLASH_DUMP_MAX))
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: FLASH READ <addr> [1..64]");
        return;
    }
    if (Logger_IsFlashBusyByLogger() || (W25q_Read(addr, buf, len) != STATUS_OK))
    {
        ReplyErr(CERR_BAD_STATE, "READ_FAIL");
        return;
    }
    (void)Uart_Printf("OK FLASH READ %lu %lu\r\n", (unsigned long)addr, (unsigned long)len);
    for (i = 0u; i < len; i++)
    {
        (void)Uart_Printf("%s%02X", ((i % FLASH_DUMP_COLS) == 0u) ? "  " : " ", buf[i]);
        if (((i % FLASH_DUMP_COLS) == (FLASH_DUMP_COLS - 1u)) || (i == (len - 1u)))
        {
            (void)Uart_Puts("\r\n");
        }
    }
}

static void Cmd_Flash(const char *sub, const char *a1, const char *a2)
{
    w25q_probe_t p;

    if ((sub == NULL) || (strcmp(sub, "ID") == 0))
    {
        PrintProbe(0u, false);
    }
    else if (strcmp(sub, "PROBE") == 0)
    {
        (void)W25q_Probe(&p);
        PrintProbe(0u, false);
    }
    else if (strcmp(sub, "TEST") == 0)
    {
        Cmd_FlashTest(a1);
    }
    else if (strcmp(sub, "READ") == 0)
    {
        Cmd_FlashRead(a1, a2);
    }
    else
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: FLASH [ID|PROBE|TEST [sector]|READ <addr> [n]]");
    }
}

/* ------------------------- Cac lenh -------------------------------- */
static void Cmd_Help(void)
{
    (void)Uart_Printf("Console dang dung cong %s\r\n", Uart_GetActiveName());
    (void)Uart_Puts("OK HELP\r\n"
                    "  STATUS | START | STOP | CLR\r\n"
                    "  DUMP [LAST <n>] (CSV) | ERASE [ALL]  (go phim bat ky de dung DUMP)\r\n"
                    "  SET LOG <ms> | LOG ON | LOG OFF | TEST <n>\r\n"
                    "  ADC | TIMING | SET VREF <mV|0>\r\n"
                    "  CAL <LM35|POT|VBG> <gain x10000> <offset mV> | CAL\r\n"
                    "  FLASH ID | FLASH PROBE | FLASH TEST [0..15] | FLASH READ <addr> [n]\r\n"
                    "  TIME | TIME SET YYYY-MM-DD HH:MM:SS | I2C | I2C SCAN | I2C SPEED 100|400\r\n");
}

static void Cmd_Status(void)
{
    uart_stats_t st;
    uint32_t     now = Tick_GetMs();

    Uart_GetStats(&st);
    (void)Uart_Printf("OK STATUS uptime=%lu ms state=%s log=%s period=%lu ms\r\n",
                      (unsigned long)now,
                      Logger_StateName(Logger_GetState()),
                      s_log_on ? "ON" : "OFF",
                      (unsigned long)s_log_period);
    (void)Uart_Printf("  uart console=%s\r\n", Uart_GetActiveName());
    (void)Uart_Printf("  uart rx: %s=%lu  %s=%lu\r\n",
                      Uart_GetPortName(0u), (unsigned long)Uart_GetPortRx(0u),
                      Uart_GetPortName(1u), (unsigned long)Uart_GetPortRx(1u));
    (void)Uart_Printf("  uart baud=%lu rx=%lu tx=%lu rx_drop=%lu tx_drop=%lu\r\n",
                      (unsigned long)st.baud_actual, (unsigned long)st.rx_bytes,
                      (unsigned long)st.tx_bytes, (unsigned long)st.rx_dropped,
                      (unsigned long)st.tx_dropped);
    (void)Uart_Printf("  hw_err OR=%lu FE=%lu NF=%lu peak rx=%lu/%u tx=%lu/%u\r\n",
                      (unsigned long)st.err_overrun, (unsigned long)st.err_framing,
                      (unsigned long)st.err_noise,
                      (unsigned long)st.rx_peak, (unsigned)CFG_UART_RX_BUF_SIZE,
                      (unsigned long)st.tx_peak, (unsigned)CFG_UART_TX_BUF_SIZE);
    {
        sensor_stats_t ss;

        Sensor_GetStats(&ss);
        (void)Uart_Printf("  adc samples=%lu missed=%lu vref_fallback=%lu vref_override=%lu\r\n",
                          (unsigned long)ss.samples, (unsigned long)ss.missed,
                          (unsigned long)ss.vref_fallback,
                          (unsigned long)Sensor_GetVrefOverride());
    }
    PrintLoggerStatus();
    PrintRtcLine(" ");
    PrintDhtLine();
}

static void PrintDhtLine(void)
{
    dht11_info_t di;
    int32_t      a;

    Dht11_GetInfo(&di);
    a = (di.temp_x10 < 0) ? -(int32_t)di.temp_x10 : (int32_t)di.temp_x10;
    if (di.valid)
    {
        (void)Uart_Printf("  dht11 T=%s%ld.%ld C H=%u.%u %% ok=%lu err_to=%lu err_crc=%lu read=%lu us\r\n",
                          (di.temp_x10 < 0) ? "-" : "", (long)(a / 10), (long)(a % 10),
                          (unsigned)(di.hum_x10 / 10u), (unsigned)(di.hum_x10 % 10u),
                          (unsigned long)di.ok, (unsigned long)di.err_timeout,
                          (unsigned long)di.err_checksum, (unsigned long)di.last_read_us);
    }
    else
    {
        (void)Uart_Printf("  dht11 KHONG CO DU LIEU (ok=%lu err_to=%lu err_crc=%lu buoc_loi=%u) - kiem tra DATA J8-11\r\n",
                          (unsigned long)di.ok, (unsigned long)di.err_timeout,
                          (unsigned long)di.err_checksum, (unsigned)di.last_err_stage);
    }
}

static void PrintLoggerStatus(void)
{
    logger_info_t li;

    Logger_GetInfo(&li);
    (void)Uart_Printf("  logger state=%s records=%lu/%lu (%lu%%) session=%u skipped=%lu\r\n",
                      Logger_StateName(li.state), (unsigned long)li.count, (unsigned long)li.capacity,
                      (unsigned long)((li.capacity != 0u) ? ((li.count * 100u) / li.capacity) : 0u),
                      (unsigned)li.session, (unsigned long)li.skipped_slots);
    (void)Uart_Printf("  logger written=%lu dropped=%lu write_err=%lu queue_peak=%lu/%u\r\n",
                      (unsigned long)li.written, (unsigned long)li.dropped,
                      (unsigned long)li.write_errors, (unsigned long)li.queue_peak,
                      (unsigned)CFG_LOG_QUEUE_LEN);
    if (li.state == LOGGER_ERASING)
    {
        (void)Uart_Printf("  erase %lu/%lu\r\n", (unsigned long)li.erase_done, (unsigned long)li.erase_total);
    }
}

static void Cmd_Dump(const char *a1, const char *a2)
{
    uint32_t n = 0u;

    if ((a1 != NULL) && (strcmp(a1, "LAST") == 0))
    {
        if ((!ParseU32(a2, &n)) || (n == 0u))
        {
            ReplyErr(CERR_BAD_ARG, "USAGE: DUMP [LAST <n>]");
            return;
        }
    }
    else if (a1 != NULL)
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: DUMP [LAST <n>]");
        return;
    }
    else
    {
        /* DUMP tat ca */
    }
    if (Logger_Dump(n) != STATUS_OK)
    {
        (void)Uart_Printf("ERR %u LOGGER_%s (STOP truoc)\r\n", (unsigned)CERR_BAD_STATE,
                          Logger_StateName(Logger_GetState()));
        return;
    }
    s_suppress_prompt = true;               /* Logger in prompt khi DUMP xong */
}

static void Cmd_Erase(const char *a1)
{
    bool all = (a1 != NULL) && (strcmp(a1, "ALL") == 0);

    if ((a1 != NULL) && (!all))
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: ERASE [ALL]");
        return;
    }
    if (Logger_Erase(all) != STATUS_OK)
    {
        (void)Uart_Printf("ERR %u LOGGER_%s (STOP truoc)\r\n", (unsigned)CERR_BAD_STATE,
                          Logger_StateName(Logger_GetState()));
        return;
    }
    (void)Uart_Printf("OK ERASE %s started%s\r\n", all ? "ALL" : "USED",
                      all ? " (chip erase 20-100 s, go STATUS de xem)" : "");
    s_suppress_prompt = true;
}

/* ------------------------- RTC / I2C ------------------------------ */
static void PrintRtcLine(const char *tag)
{
    timesvc_info_t ti;
    rtc_time_t     t;
    char           dt[24];
    int32_t        a;

    TimeSvc_GetInfo(&ti);
    if (!ti.found)
    {
        (void)Uart_Printf("%s rtc KHONG THAY DS3231 (I2C 0x68) - kiem tra SDA J8-12 / SCL J8-10\r\n", tag);
        return;
    }
    a = (ti.rtc_temp_x100 < 0) ? -ti.rtc_temp_x100 : ti.rtc_temp_x100;
    if (TimeSvc_Now(&t))
    {
        Rtc_Format(&t, dt, sizeof(dt));
    }
    else
    {
        (void)strcpy(dt, ti.osc_stopped ? "CHUA DAT GIO (go TIME SET)" : "LOI DOC");
    }
    (void)Uart_Printf("%s rtc %s  rtc_temp=%s%ld.%02ld C  i2c=%lukHz read=%luus err=%lu\r\n",
                      tag, dt, (ti.rtc_temp_x100 < 0) ? "-" : "",
                      (long)(a / 100), (long)(a % 100), (unsigned long)I2c_GetSpeedKhz(),
                      (unsigned long)ti.last_read_us, (unsigned long)ti.read_errors);
}

static void Cmd_Time(const char *sub, const char *date, const char *time)
{
    rtc_time_t t;

    if (sub == NULL)
    {
        PrintRtcLine("OK TIME");
        return;
    }
    if ((strcmp(sub, "SET") != 0) || (!Rtc_Parse(date, time, &t)))
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: TIME SET YYYY-MM-DD HH:MM:SS");
        return;
    }
    {
        status_t    st = TimeSvc_Set(&t);
        i2c_stats_t is;

        if (st != STATUS_OK)
        {
            I2c_GetStats(&is);
            (void)Uart_Printf("ERR %u RTC_WRITE_FAIL st=%u nack=%lu timeout=%lu (kiem tra day SDA/SCL)\r\n",
                              (unsigned)CERR_BAD_STATE, (unsigned)st,
                              (unsigned long)is.nack, (unsigned long)is.timeout);
            return;
        }
    }
    PrintRtcLine("OK TIME SET");
}

static void Cmd_I2c(const char *sub, const char *arg)
{
    uint32_t    v;
    uint32_t    a;
    uint32_t    found = 0u;
    uint32_t    t0;
    i2c_stats_t st;

    if ((sub != NULL) && (strcmp(sub, "SCAN") == 0))
    {
        (void)Uart_Puts("OK I2C SCAN:");
        for (a = 0x08u; a < 0x78u; a++)
        {
            if (I2c_Probe((uint8_t)a) == STATUS_OK)
            {
                (void)Uart_Printf(" 0x%02lX", (unsigned long)a);
                found++;
            }
        }
        (void)Uart_Printf("  (%lu thiet bi; 0x68=DS3231 0x57=EEPROM)\r\n", (unsigned long)found);
    }
    else if ((sub != NULL) && (strcmp(sub, "SPEED") == 0) && ParseU32(arg, &v) &&
             (I2c_Init(v) == STATUS_OK))
    {
        rtc_time_t tt;

        t0 = Tick_GetUs();
        (void)Ds3231_GetTime(&tt);
        (void)Uart_Printf("OK I2C SPEED %lu kHz, doc RTC 7 byte = %lu us\r\n",
                          (unsigned long)v, (unsigned long)(Tick_GetUs() - t0));
    }
    else if (sub == NULL)
    {
        I2c_GetStats(&st);
        (void)Uart_Printf("OK I2C %lu kHz transfers=%lu nack=%lu timeout=%lu arb_lost=%lu\r\n",
                          (unsigned long)I2c_GetSpeedKhz(), (unsigned long)st.transfers,
                          (unsigned long)st.nack, (unsigned long)st.timeout, (unsigned long)st.arb_lost);
    }
    else
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: I2C | I2C SCAN | I2C SPEED 100|400");
    }
}

static void Cmd_Adc(void)
{
    sensor_sample_t m;
    uint32_t        i;

    if (!Sensor_GetLatest(&m))
    {
        ReplyErr(CERR_BAD_STATE, "NO_SAMPLE_YET");
        return;
    }
    PrintSample("OK ADC", &m, Tick_GetMs());
    for (i = 0u; i < (uint32_t)SENSOR_COUNT; i++)
    {
        (void)Uart_Printf("  %-4s raw=%4u mv=%ld\r\n", Sensor_GetName((sensor_ch_t)i),
                          (unsigned)m.raw[i], (long)m.mv[i]);
    }
    PrintDhtLine();
}

static void Cmd_Timing(const char *arg)
{
    sample_timing_t t;

    if ((arg != NULL) && (strcmp(arg, "RESET") == 0))
    {
        SampleTimer_ResetTiming();
        (void)Uart_Puts("OK TIMING RESET\r\n");
        return;
    }
    SampleTimer_GetTiming(&t);
    if (t.count < 2u)
    {
        ReplyErr(CERR_BAD_STATE, "NOT_ENOUGH_TICKS");
        return;
    }
    (void)Uart_Printf("OK TIMING n=%lu period=%lu us min=%lu us max=%lu us jitter=%ld/+%ld us\r\n",
                      (unsigned long)t.count, (unsigned long)t.period_us,
                      (unsigned long)t.min_us, (unsigned long)t.max_us,
                      (long)t.min_us - (long)t.period_us,
                      (long)t.max_us - (long)t.period_us);
}

static void Cmd_Cal(const char *name, const char *gain_s, const char *ofs_s)
{
    sensor_ch_t  ch;
    sensor_cal_t c;
    int32_t      gain;
    int32_t      ofs;
    uint32_t     i;

    if (name == NULL)                                /* CAL: in bang hieu chuan */
    {
        (void)Uart_Puts("OK CAL\r\n");
        for (i = 0u; i < (uint32_t)SENSOR_COUNT; i++)
        {
            Sensor_GetCal((sensor_ch_t)i, &c);
            (void)Uart_Printf("  %-4s gain=%ld offset=%ld mV\r\n",
                              Sensor_GetName((sensor_ch_t)i), (long)c.gain, (long)c.offset_mv);
        }
        return;
    }
    if ((!ParseSensor(name, &ch)) || (!ParseI32(gain_s, &gain)) || (!ParseI32(ofs_s, &ofs)) ||
        (Sensor_SetCal(ch, gain, ofs) != STATUS_OK))
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: CAL <LM35|POT|VBG> <gain>0> <offset>");
        return;
    }
    (void)Uart_Printf("OK CAL %s gain=%ld offset=%ld\r\n", name, (long)gain, (long)ofs);
}

static void Cmd_Start(void)
{
    logger_state_t ls = Logger_GetState();

    if (ls == LOGGER_RUNNING)
    {
        ReplyErr(CERR_BAD_STATE, "ALREADY_RUNNING");
        return;
    }
    if (ls == LOGGER_NO_FLASH)
    {
        ReplyErr(CERR_NO_STORAGE, "NO_FLASH (chi in LOG, khong ghi)");
    }
    else if (Logger_Start() != STATUS_OK)
    {
        (void)Uart_Printf("ERR %u LOGGER_%s\r\n", (unsigned)CERR_BAD_STATE, Logger_StateName(ls));
        return;
    }
    else
    {
        /* Dang ghi vao Flash */
    }
    s_running       = true;
    s_session_start = Tick_GetMs();
    s_log_last      = s_session_start;
    {
        logger_info_t li;

        Logger_GetInfo(&li);
        (void)Uart_Printf("OK START session=%u slot=%lu/%lu rate=%lu ms\r\n",
                          (unsigned)li.session, (unsigned long)li.count,
                          (unsigned long)li.capacity, (unsigned long)CFG_SAMPLE_PERIOD_MS);
    }
}

static void Cmd_Stop(void)
{
    if (!s_running)
    {
        ReplyErr(CERR_BAD_STATE, "NOT_RUNNING");
        return;
    }
    s_running = false;
    (void)Logger_Stop();
    {
        logger_info_t li;

        Logger_GetInfo(&li);
        (void)Uart_Printf("OK STOP duration=%lu ms written=%lu dropped=%lu write_err=%lu queue_peak=%lu\r\n",
                          (unsigned long)(Tick_GetMs() - s_session_start), (unsigned long)li.written,
                          (unsigned long)li.dropped, (unsigned long)li.write_errors,
                          (unsigned long)li.queue_peak);
    }
}

static void Cmd_Set(const char *param, const char *value)
{
    uint32_t v;

    if ((param != NULL) && (strcmp(param, "VREF") == 0))
    {
        if ((!ParseU32(value, &v)) ||
            ((v != 0u) && ((v < CFG_ADC_VREF_MIN_MV) || (v > CFG_ADC_VREF_MAX_MV))))
        {
            ReplyErr(CERR_BAD_ARG, "RANGE 0 (auto) | 2700..5500");
            return;
        }
        Sensor_SetVrefOverride(v);
        (void)Uart_Printf("OK SET VREF %lu%s\r\n", (unsigned long)v, (v == 0u) ? " (auto)" : "");
        return;
    }
    if ((param == NULL) || (strcmp(param, "LOG") != 0))
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: SET LOG <ms> | SET VREF <mV>");
        return;
    }
    if ((!ParseU32(value, &v)) || (v < CFG_LOG_PERIOD_MIN_MS) || (v > CFG_LOG_PERIOD_MAX_MS))
    {
        ReplyErr(CERR_BAD_ARG, "RANGE 100..60000");
        return;
    }
    s_log_period = v;
    s_log_last   = Tick_GetMs();
    (void)Uart_Printf("OK SET LOG %lu\r\n", (unsigned long)v);
}

static void Cmd_Log(const char *arg)
{
    if ((arg != NULL) && (strcmp(arg, "ON") == 0))
    {
        s_log_on   = true;
        s_log_last = Tick_GetMs();
        (void)Uart_Puts("OK LOG ON\r\n");
    }
    else if ((arg != NULL) && (strcmp(arg, "OFF") == 0))
    {
        s_log_on = false;
        (void)Uart_Puts("OK LOG OFF\r\n");
    }
    else
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: LOG ON|OFF");
    }
}

static void Cmd_Test(const char *arg)
{
    uint32_t n;

    if ((!ParseU32(arg, &n)) || (n == 0u) || (n > CFG_ECHO_MAX_BYTES))
    {
        ReplyErr(CERR_BAD_ARG, "USAGE: TEST <1..1000000>");
        return;
    }
    Uart_ClearStats();
    s_test_total   = n;
    s_test_got     = 0u;
    s_test_crc     = CRC32_INIT;
    s_test_t0      = Tick_GetMs();
    s_test_last_rx = s_test_t0;
    s_mode         = CMODE_TEST;
    (void)Uart_Printf("OK READY %lu\r\n", (unsigned long)n);
}

/* ------------------------------------------------------------------ */
static void ExecuteLine(char *line)
{
    char    *argv[CFG_CMD_ARGS_MAX] = { NULL };
    uint32_t argc = 0u;
    char    *p;
    char    *tok;

    for (p = line; *p != '\0'; p++)
    {
        *p = (char)toupper((unsigned char)*p);
    }

    tok = strtok(line, " \t");
    while ((tok != NULL) && (argc < CFG_CMD_ARGS_MAX))
    {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t");
    }
    if (argc == 0u)
    {
        return;                                   /* Dong trong */
    }

    if      (strcmp(argv[0], "HELP")   == 0) { Cmd_Help(); }
    else if (strcmp(argv[0], "STATUS") == 0) { Cmd_Status(); }
    else if (strcmp(argv[0], "START")  == 0) { Cmd_Start(); }
    else if (strcmp(argv[0], "STOP")   == 0) { Cmd_Stop(); }
    else if (strcmp(argv[0], "SET")    == 0) { Cmd_Set(argv[1], argv[2]); }
    else if (strcmp(argv[0], "LOG")    == 0) { Cmd_Log(argv[1]); }
    else if (strcmp(argv[0], "TEST")   == 0) { Cmd_Test(argv[1]); }
    else if (strcmp(argv[0], "ADC")    == 0) { Cmd_Adc(); }
    else if (strcmp(argv[0], "TIMING") == 0) { Cmd_Timing(argv[1]); }
    else if (strcmp(argv[0], "CAL")    == 0) { Cmd_Cal(argv[1], argv[2], argv[3]); }
    else if (strcmp(argv[0], "FLASH")  == 0) { Cmd_Flash(argv[1], argv[2], argv[3]); }
    else if (strcmp(argv[0], "TIME")   == 0) { Cmd_Time(argv[1], argv[2], argv[3]); }
    else if (strcmp(argv[0], "I2C")    == 0) { Cmd_I2c(argv[1], argv[2]); }
    else if (strcmp(argv[0], "CLR")    == 0) { Uart_ClearStats(); (void)Uart_Puts("OK CLR\r\n"); }
    else if (strcmp(argv[0], "DUMP")   == 0) { Cmd_Dump(argv[1], argv[2]); }
    else if (strcmp(argv[0], "ERASE")  == 0) { Cmd_Erase(argv[1]); }
    else
    {
        ReplyErr(CERR_UNKNOWN_CMD, "UNKNOWN_CMD (go HELP)");
    }
}

static void Prompt(void)
{
    (void)Uart_Puts(CONSOLE_PROMPT);
}

/* Nhan 1 byte o che do dong lenh: echo lai cho terminal, xu ly Backspace,
   Enter (CR, LF hoac CRLF) -> thuc thi. */
static void HandleLineByte(uint8_t c)
{
    static uint8_t prev = 0u;
    uint8_t        last = prev;

    prev = c;
    if ((c == (uint8_t)'\n') && (last == (uint8_t)'\r'))
    {
        return;                                    /* CRLF: LF thu 2 bo qua */
    }
    if ((c == (uint8_t)'\r') || (c == (uint8_t)'\n'))
    {
        (void)Uart_Puts("\r\n");
        if (s_line_overflow)
        {
            ReplyErr(CERR_LINE_LONG, "LINE_TOO_LONG");
        }
        else if (s_line_len > 0u)
        {
            s_line[s_line_len] = '\0';
            ExecuteLine(s_line);
        }
        s_line_len      = 0u;
        s_line_overflow = false;
        if ((s_mode == CMODE_LINE) && (!s_suppress_prompt))   /* TEST/DUMP/ERASE in prompt khi xong */
        {
            Prompt();
        }
        s_suppress_prompt = false;
    }
    else if ((c == ASCII_BACKSPACE) || (c == ASCII_DEL))
    {
        if (s_line_len > 0u)
        {
            s_line_len--;
            (void)Uart_Puts("\b \b");            /* Xoa ky tu tren man hinh */
        }
    }
    else if ((c < ASCII_PRINT_MIN) || (c > ASCII_PRINT_MAX))
    {
        /* Bo ky tu dieu khien khac (mui ten, ESC...) */
    }
    else if (s_line_len < (CFG_CMD_LINE_MAX - 1u))
    {
        s_line[s_line_len++] = (char)c;
        (void)Uart_Write(&c, 1u);                  /* Echo */
    }
    else
    {
        s_line_overflow = true;                    /* Bo phan con lai den het dong */
    }
}

static void HandleTestByte(uint8_t c)
{
    s_test_crc     = Crc32_Update(s_test_crc, c);
    s_test_last_rx = Tick_GetMs();
    s_test_got++;

    if (s_test_got >= s_test_total)
    {
        uart_stats_t st;
        uint32_t     dt = Tick_GetMs() - s_test_t0;

        Uart_GetStats(&st);
        s_mode = CMODE_LINE;
        (void)Uart_Printf("OK TEST n=%lu crc=0x%08lX time=%lu ms rx_drop=%lu OR=%lu FE=%lu NF=%lu peak=%lu\r\n",
                          (unsigned long)s_test_got,
                          (unsigned long)(s_test_crc ^ CRC32_XOR_OUT),
                          (unsigned long)dt,
                          (unsigned long)st.rx_dropped, (unsigned long)st.err_overrun,
                          (unsigned long)st.err_framing, (unsigned long)st.err_noise,
                          (unsigned long)st.rx_peak);
        Prompt();
    }
}

/* ------------------------------------------------------------------ */
status_t Console_Init(void)
{
    s_line_len      = 0u;
    s_line_overflow = false;
    s_mode          = CMODE_LINE;
    s_running       = false;
    s_log_on        = true;
    s_log_period    = CFG_LOG_PERIOD_DEFAULT_MS;
    s_log_last      = Tick_GetMs();

    (void)Uart_Puts("\r\n=== PEB301 P2-03 Vehicle Data Logger ===\r\n"
                    "Phase 1: UART console. Go HELP.\r\n");
    s_alive_last    = Tick_GetMs();
    return STATUS_OK;
}

void Console_Poll(void)
{
    uint8_t c;

    while (Uart_ReadByte(&c))
    {
        if ((Logger_GetState() == LOGGER_DUMPING) || (Logger_GetState() == LOGGER_ERASING))
        {
            Logger_Abort();                         /* Phim bat ky: dung DUMP (ERASE ALL khong dung duoc) */
            continue;
        }
        if (s_mode == CMODE_TEST)
        {
            HandleTestByte(c);
        }
        else
        {
            HandleLineByte(c);
        }
    }

    /* Timeout khi dang TEST ma PC ngung gui */
    if ((s_mode == CMODE_TEST) &&
        ((Tick_GetMs() - s_test_last_rx) > TEST_TIMEOUT_MS))
    {
        s_mode = CMODE_LINE;
        (void)Uart_Printf("ERR %u TIMEOUT got=%lu/%lu\r\n", (unsigned)CERR_TIMEOUT,
                          (unsigned long)s_test_got, (unsigned long)s_test_total);
        Prompt();
    }
}

/* Chua khoa cong: moi 2 s phat 1 dong ALIVE rieng tren tung cong, ghi ro ten cong
   -> terminal nao thay dong nay thi cong do dang thong. Go Enter de khoa. */
static void AliveTask(void)
{
    uint32_t i;

    if (Uart_IsLocked() || (!Tick_Elapsed(&s_alive_last, ALIVE_PERIOD_MS)))
    {
        return;
    }
    sensor_sample_t m;
    bool            has = Sensor_GetLatest(&m);

    for (i = 0u; i < UART_PORT_COUNT; i++)
    {
        (void)Uart_PrintfTo(i, "ALIVE t=%lu ms via %s rx=%lu - go Enter o bat ky cong nao\r\n",
                            (unsigned long)Tick_GetMs(), Uart_GetPortName(i),
                            (unsigned long)Uart_GetPortRx(i));
        if (has)
        {
            (void)Uart_PrintfTo(i, "  adc raw LM35=%u POT=%u VBG=%u -> vdd=%lu mV seq=%lu\r\n",
                                (unsigned)m.raw[SENSOR_LM35], (unsigned)m.raw[SENSOR_POT],
                                (unsigned)m.raw[SENSOR_BANDGAP], (unsigned long)m.vref_mv,
                                (unsigned long)m.seq);
        }
        PrintProbe(i, true);
        {
            timesvc_info_t ti;
            rtc_time_t     t;
            char           dt[24];

            TimeSvc_GetInfo(&ti);
            if (!ti.found)
            {
                (void)Uart_PrintfTo(i, "  rtc KHONG THAY DS3231 (I2C nack=%lu)\r\n", 0ul);
            }
            else if (TimeSvc_Now(&t))
            {
                Rtc_Format(&t, dt, sizeof(dt));
                (void)Uart_PrintfTo(i, "  rtc %s temp=%ld C read=%lu us\r\n", dt,
                                    (long)(ti.rtc_temp_x100 / 100), (unsigned long)ti.last_read_us);
            }
            else
            {
                (void)Uart_PrintfTo(i, "  rtc OK nhung CHUA DAT GIO (osf=%u)\r\n", (unsigned)ti.osc_stopped);
            }
        }
    }
}

void Console_LogTask(void)
{
    AliveTask();

    if ((!s_running) || (!s_log_on) || (s_mode != CMODE_LINE))
    {
        return;
    }
    if (Tick_Elapsed(&s_log_last, s_log_period))
    {
        /* Giai doan sau: ghi vao Flash + timestamp RTC */
        sensor_sample_t m;

        if (Sensor_GetLatest(&m))
        {
            PrintSample("LOG", &m, Tick_GetMs() - s_session_start);
        }
    }
}
