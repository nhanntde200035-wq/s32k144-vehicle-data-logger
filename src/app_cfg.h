/*
 * app_cfg.h - Toan bo hang so cau hinh cua do an P2-03 (khong dung magic number)
 */
#ifndef APP_CFG_H
#define APP_CFG_H

/* ---------------- Clock ---------------- */
#define CFG_SYS_CLK_HZ              48000000u   /* Core = FIRC 48 MHz        */
#define CFG_PERIPH_CLK_HZ           48000000u   /* FIRCDIV2 = 48 MHz         */
#define CFG_PCC_PCS_FIRCDIV2        3u          /* PCC PCS: 3 = FIRCDIV2     */
#define CFG_CLK_TIMEOUT             100000u     /* So vong cho toi da        */

/* ---------------- UART (LPUART2 -> header J8 cua MaaZEDU) ----------------
 * J8-14 = LPUART2_TX (noi vao RX cua USB-TTL)
 * J8-16 = LPUART2_RX (noi vao TX cua USB-TTL)
 * S32K144 co 2 cap chan LPUART2 ALT2: PTD6/PTD7 va PTA8/PTA9.
 * Tai lieu board khong ghi ro -> khi khoi dong, firmware tu do chan RX nao
 * dang o muc cao (duong UART ranh luon = 1) de chon cap chan dung.
 * TX duoc xuat ra ca 2 chan nen luon thay du lieu tren J8-14.          */
#define CFG_UART_BAUD               115200u
#define CFG_UART_OSR                16u         /* Oversampling ratio        */
#define CFG_UART_MAX_ERR_PERMILLE   20u         /* Sai so baud toi da 2,0 %  */
#define CFG_UART_PIN_MUX            2u          /* ALT2 = LPUART2            */
#define CFG_UART_PD_RX_PIN          6u          /* PTD6 = LPUART2_RX         */
#define CFG_UART_PD_TX_PIN          7u          /* PTD7 = LPUART2_TX         */
#define CFG_UART_PA_RX_PIN          8u          /* PTA8 = LPUART2_RX         */
#define CFG_UART_PA_TX_PIN          9u          /* PTA9 = LPUART2_TX         */
#define CFG_UART_DETECT_SETTLE      2000u       /* Vong cho pull-down on dinh */
#define CFG_UART_RX_BUF_SIZE        256u        /* Bat buoc luy thua cua 2   */
#define CFG_UART_TX_BUF_SIZE        1024u       /* Bat buoc luy thua cua 2   */

/* ---------------- Tick he thong (LPIT0 kenh 0) ---------------- */
#define CFG_TICK_HZ                 1000u       /* 1 ms                      */
#define CFG_LPIT_EN_DELAY           16u         /* Cho sau khi bat M_CEN     */

/* ---------------- Lay mau ADC (LPIT0 kenh 1 + ADC0) ----------------------
 * J8-18 = ADC0_SE12 (PTC14) : LM35  (VS = J8-01 5V, GND = J8-24)
 * J8-20 = ADC0_SE13 (PTC15) : bien tro (2 dau = J8-22 VDD / J8-24 GND)
 * Kenh 3 = bandgap noi (~1,0 V) -> tu tinh VDD (= VREFH) thuc te       */
#define CFG_SAMPLE_PERIOD_MS        100u        /* 10 Hz                     */
#define CFG_ADC_CH_LM35             12u         /* ADC0_SE12                 */
#define CFG_ADC_CH_POT              13u         /* ADC0_SE13                 */
#define CFG_ADC_CH_BANDGAP          27u         /* ADCH = 0x1B: bandgap noi  */
#define CFG_ADC_PIN_LM35            14u         /* PTC14                     */
#define CFG_ADC_PIN_POT             15u         /* PTC15                     */
#define CFG_ADC_FULL_SCALE          4095u       /* 12 bit                    */
#define CFG_ADC_BANDGAP_MV          1000u       /* VBG danh dinh (datasheet) */
#define CFG_ADC_VREF_DEFAULT_MV     5000u       /* Dung khi bandgap loi      */
#define CFG_ADC_VREF_MIN_MV         2700u       /* Gioi han hop ly cua VDD   */
#define CFG_ADC_VREF_MAX_MV         5500u
#define CFG_ADC_CAL_TIMEOUT         1000000u    /* Vong cho tu hieu chuan    */
#define CFG_CAL_GAIN_ONE            10000       /* He so gain 1,0000         */
#define CFG_LM35_MV_PER_DEGC        10          /* LM35: 10 mV/°C            */
/* Bu lech LM35 mac dinh (mV, 10 mV = 1 °C). Do doi chieu DHT11 + DS3231 ngay
   2026-10-23: LM35 lech trung binh +22,3 °C -> -223 mV. Thay LM35 moi -> dat 0. */
#define CFG_LM35_OFFSET_MV          (-223)

/* ---------------- SPI Flash W25Qxx (LPSPI0 tren header J8) --------------
 * J8-04 = SCK  (PTB2, ALT3)      J8-02 = SIN/MISO  (PTB3, ALT3)
 * J8-06 = SOUT (PTB4, ALT3)      J8-08 = PCS0 (PTB5) -> dung lam GPIO CS
 * Tai lieu board khong ghi ro chan -> CS xuat song song ra PTB5 va PTB0,
 * SOUT xuat ra PTB4 va PTB1; driver tu thu PINCFG 0/3 (dao SIN<->SOUT).  */
#define CFG_SPI_SCK_PIN             2u          /* PTB2  LPSPI0_SCK          */
#define CFG_SPI_SIN_PIN             3u          /* PTB3  LPSPI0_SIN          */
#define CFG_SPI_SOUT_PIN            4u          /* PTB4  LPSPI0_SOUT         */
#define CFG_SPI_SOUT_ALT_PIN        1u          /* PTB1  LPSPI0_SOUT (phu)   */
#define CFG_SPI_CS_PIN              5u          /* PTB5  CS (GPIO)           */
#define CFG_SPI_CS_ALT_PIN          0u          /* PTB0  CS (GPIO, phu)      */
#define CFG_SPI_PIN_MUX             3u          /* ALT3                      */
#define CFG_SPI_SCK_HZ              4000000u    /* 4 MHz: an toan tren breadboard */
#define CFG_SPI_PROBE_SCK_HZ        1000000u    /* 1 MHz khi do chan         */
#define CFG_SPI_BYTE_TIMEOUT        10000u      /* Vong cho moi byte         */

#define CFG_FLASH_PAGE_SIZE         256u
#define CFG_FLASH_SECTOR_SIZE       4096u
#define CFG_FLASH_TIMEOUT_PAGE_MS   10u         /* tPP max 3 ms              */
#define CFG_FLASH_TIMEOUT_SECTOR_MS 1000u       /* tSE max 400 ms            */
#define CFG_FLASH_TIMEOUT_CHIP_MS   200000u     /* tCE max 100 s (W25Q64)    */

/* ---------------- I2C (LPI2C0) + RTC DS3231 ----------------------------
 * J8-12 = SDA (PTA2, ALT3)   J8-10 = SCL (PTA3, ALT3)
 * Module ZS-042 da co dien tro keo len 4,7 kOhm.                        */
#define CFG_I2C_SDA_PIN             2u          /* PTA2 LPI2C0_SDA           */
#define CFG_I2C_SCL_PIN             3u          /* PTA3 LPI2C0_SCL           */
#define CFG_I2C_PIN_MUX             3u          /* ALT3                      */
#define CFG_I2C_SPEED_KHZ           100u        /* 100 hoac 400              */
#define CFG_I2C_TIMEOUT             20000u      /* Vong cho moi buoc         */
#define CFG_DS3231_ADDR             0x68u
#define CFG_EEPROM_ADDR             0x57u       /* AT24C32 tren module ZS-042 */
#define CFG_RTC_READ_PERIOD_MS      1000u

/* ---------------- TFT ST7735 1.8" 128x160 (chung LPSPI0 voi W25Q) ------
 * J8-04 SCK, J8-06 MOSI (chung) ; CS = J8-05 PTE0 ; A0/DC = J8-07 PTD17 ;
 * RESET = J8-09 PTD14 ; LED (den nen) -> 3,3 V                          */
#define CFG_TFT_CS_PIN              0u          /* PTE0                      */
#define CFG_TFT_DC_PIN              17u         /* PTD17                     */
#define CFG_TFT_RST_PIN             14u         /* PTD14                     */
#define CFG_TFT_SCK_HZ              8000000u    /* ST7735 toi da ~15 MHz     */
#define CFG_TFT_WIDTH               128u
#define CFG_TFT_HEIGHT              160u
#define CFG_DISPLAY_PERIOD_MS       500u        /* Chu ky cap nhat man hinh  */

/* ---------------- DHT11 (nhiet do + do am, 1 day) ---------------------
 * J8-11 = GPIO3 = PTD13 ; VCC = 3,3 V (J8-22) ; GND = J8-24               */
#define CFG_DHT_PORT                PORTD
#define CFG_DHT_GPIO                PTD
#define CFG_DHT_PCC_INDEX           PCC_PORTD_INDEX
#define CFG_DHT_PIN                 13u         /* PTD13                     */
#define CFG_DHT_PERIOD_MS           1000u       /* DHT11 toi da 1 lan/giay   */

/* ---------------- Logger ----------------------------------------------- */
#define CFG_LOG_TEST_AREA_BYTES     65536u      /* 64 KB cuoi Flash danh cho FLASH TEST */
#define CFG_LOG_QUEUE_LEN           32u         /* Hang doi ban ghi trong RAM (3,2 s @10 Hz) */
#define CFG_LOG_DUMP_LINE_MAX       112u        /* Byte toi da 1 dong CSV   */

/* ---------------- Uu tien ngat (0 = cao nhat, 15 = thap nhat) ---------- */
#define CFG_IRQ_PRIO_TICK           2u
#define CFG_IRQ_PRIO_SAMPLE         1u          /* Nhip lay mau uu tien nhat */
#define CFG_IRQ_PRIO_ADC            2u
#define CFG_IRQ_PRIO_UART           3u

/* ---------------- LED trang thai (LED RGB tren EVB, muc thap = sang) --- */
#define CFG_LED_PIN                 0u          /* PTD0 = LED xanh duong     */
#define CFG_LED_PERIOD_MS           500u

/* ---------------- Console ---------------- */
#define CFG_CMD_LINE_MAX            64u
#define CFG_CMD_ARGS_MAX            4u
#define CFG_LOG_PERIOD_DEFAULT_MS   1000u
#define CFG_LOG_PERIOD_MIN_MS       100u
#define CFG_LOG_PERIOD_MAX_MS       60000u
#define CFG_ECHO_MAX_BYTES          1000000u

#endif /* APP_CFG_H */
