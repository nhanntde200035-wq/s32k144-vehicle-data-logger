# Vehicle Data Logger — S32K144 (bare-metal)

> 🇬🇧 English below · 🇻🇳 [Tiếng Việt ở phần sau](#tiếng-việt)

Bare-metal firmware for the NXP **S32K144** (ARM Cortex-M4F) that samples analog and digital sensors, stamps each sample with real time from an RTC, stores it as CRC-protected records in external SPI Flash, shows live data on a TFT display, and is fully controlled through a UART command console.

Course project **PEB301 – P2-03 (advanced level)**, FPT University Da Nang.
Firmware written by **Nguyễn Thiện Nhân** (team lead).

---

## English

### Features
- **Register-level drivers (no SDK):** clock, GPIO, NVIC, LPIT, ADC, LPUART, LPSPI, LPI2C.
- **Sampling:** ADC0 at 10 Hz from a hardware timer (LPIT ch1) — LM35 temperature, potentiometer and the internal bandgap, used to compute the real VDD.
- **Calibration:** per-channel gain/offset at runtime (`CAL` command).
- **Sensors:** DHT11 temperature/humidity (single-wire), DS3231 RTC over I2C for real timestamps.
- **Storage:** W25Q64 SPI Flash, 32-byte records with **CRC-16/CCITT**, session numbers and sequence counters.
  - Power-loss tolerant: on boot the logger binary-searches the first empty slot and skips half-written slots.
  - Flash is never accessed from an ISR and the main loop never blocks on a busy Flash — records wait in a RAM queue.
- **Display:** ST7735 1.8" TFT (128×160), sharing the SPI bus with the Flash.
- **Console:** interrupt-driven UART with RX/TX ring buffers, 115200 8N1, text commands with `OK` / `ERR <code>` replies, CSV export.
- **Verified:** zero byte loss at 115200 bps checked by a Python script (CRC32 over 50 KB × 5 runs).

### Architecture
```
src/
├── app/       main.c (super-loop), console.c (command parser)
├── service/   logger, sensor, display, timesvc, ring_buffer
├── device/    w25q (Flash), ds3231 (RTC), st7735 (TFT), dht11
├── mcal/      register-level drivers: clock, gpio, nvic, lpit, adc, lpuart, lpspi, lpi2c
├── app_cfg.h  every configuration constant (no magic numbers)
└── status.h   shared status codes
tools/uart_test.py   UART byte-loss test (PC side)
```
Layers only call downward: `app → service → device → mcal`.

### Record format (32 bytes)
| Field | Size | Notes |
|---|---|---|
| magic | 2 | `0xA55A` |
| session, seq | 2 + 4 | logging session and sequence number |
| time_s, uptime_ms | 4 + 4 | RTC time and system uptime |
| raw_lm35, raw_pot | 2 + 2 | raw ADC values |
| dht_temp_x10, temp_x10, pot_x10 | 2 × 3 | scaled values (×10) |
| vdd_mv | 2 | measured supply voltage |
| flags, hum_pct | 1 + 1 | status flags, humidity |
| crc16 | 2 | CRC-16/CCITT-FALSE over the first 30 bytes |

### Hardware & wiring (MaaZEDU board, header J8)
| J8 pin | Signal (MCU pin) | Connected to |
|---|---|---|
| 01 | 5 V | LM35 +VS |
| 02 / 04 / 06 / 08 | LPSPI0 SIN / SCK / SOUT / CS (PTB3/2/4/5) | W25Q64 DO / CLK / DI / CS |
| 05 / 07 / 09 | PTE0 / PTD17 / PTD14 | ST7735 CS / A0(DC) / RESET (SCK, MOSI shared) |
| 10 / 12 | LPI2C0 SCL / SDA (PTA3/PTA2) | DS3231 module (0x68) |
| 11 | PTD13 | DHT11 data |
| 14 / 16 | LPUART2 TX / RX (PTD7/PTD6) | USB-TTL RX / TX |
| 18 / 20 | ADC0_SE12 / SE13 (PTC14/PTC15) | LM35 OUT / potentiometer |
| 22 / 24 | 3.3 V / GND | all modules |

### Build & flash
1. S32 Design Studio → **New S32DS Application Project** → S32K144, C, NewLib Nano, **no SDK**.
2. Import / copy this project, build with **Ctrl+B**.
3. Flash with the J-Link debug configuration (`Debug_FLASH`), then **Resume**.
4. Open a terminal at **115200 8N1** and type `HELP`.

### Console commands
```
STATUS | START | STOP | CLR
DUMP [LAST <n>]           export records as CSV
ERASE [ALL]
SET LOG <ms> | LOG ON | LOG OFF
ADC | TIMING | SET VREF <mV|0>
CAL <LM35|POT|VBG> <gain x10000> <offset mV>
FLASH ID | FLASH PROBE | FLASH TEST [0..15] | FLASH READ <addr> [n]
TIME | TIME SET YYYY-MM-DD HH:MM:SS
I2C | I2C SCAN | I2C SPEED 100|400
```

### UART test
```
pip install pyserial
python tools/uart_test.py COM5 --size 50000 --runs 5
```
Pass: CRC matches in all runs, `rx_drop=0 OR=0 FE=0`.

---

## Tiếng Việt

Firmware bare-metal cho **S32K144** (ARM Cortex-M4F): lấy mẫu cảm biến, gắn thời gian thực từ RTC, lưu bản ghi có CRC vào SPI Flash ngoài, hiển thị lên màn hình TFT và điều khiển toàn bộ qua console UART.

Đồ án môn **PEB301 – đề P2-03 (mức Nâng cao)**, Đại học FPT Đà Nẵng. Người viết firmware: **Nguyễn Thiện Nhân** (nhóm trưởng).

### Tính năng
- **Driver mức thanh ghi, không dùng SDK:** clock, GPIO, NVIC, LPIT, ADC, LPUART, LPSPI, LPI2C.
- **Lấy mẫu:** ADC0 10 Hz theo timer phần cứng (LPIT kênh 1). Đọc LM35, biến trở và bandgap nội để tính VDD thực tế.
- **Hiệu chuẩn:** chỉnh gain/offset từng kênh lúc chạy (lệnh `CAL`).
- **Cảm biến:** DHT11 (nhiệt độ/độ ẩm, 1 dây), RTC DS3231 qua I2C để gắn thời gian thật.
- **Lưu trữ:** Flash W25Q64, bản ghi 32 byte có **CRC-16/CCITT**, số phiên và số thứ tự.
  - Chịu mất điện: khi khởi động tìm nhị phân ô trống đầu tiên, bỏ qua ô ghi dở.
  - Không truy cập Flash trong ngắt, vòng lặp chính không bao giờ chờ Flash; bản ghi đợi trong hàng đợi RAM.
- **Hiển thị:** TFT ST7735 1.8" (128×160), dùng chung bus SPI với Flash.
- **Console:** UART ngắt + ring buffer RX/TX, 115200 8N1, trả lời dạng `OK` / `ERR <mã>`, xuất CSV.
- **Kiểm chứng:** không mất byte ở 115200 bps, kiểm bằng script Python (CRC32, 50 KB × 5 lần).

### Kiến trúc
Cấu trúc thư mục và định dạng bản ghi: xem phần tiếng Anh ở trên. Các lớp chỉ gọi xuống dưới: `app → service → device → mcal`. Mọi hằng số cấu hình nằm trong `app_cfg.h`.

### Nối dây
Xem bảng **Hardware & wiring** ở trên (header J8 của board MaaZEDU).

### Build và nạp
1. S32 Design Studio → **New S32DS Application Project** → S32K144, C, NewLib Nano, **không chọn SDK**.
2. Import project này, build bằng **Ctrl+B**.
3. Nạp bằng cấu hình J-Link (`Debug_FLASH`) rồi **Resume**.
4. Mở terminal **115200 8N1**, gõ `HELP` để xem danh sách lệnh.

### Ghi chú
- Cảm biến LM35 đang dùng bị lệch khoảng +22 °C so với DHT11, đã bù bằng `CFG_LM35_OFFSET_MV` trong `app_cfg.h`. Thay LM35 mới thì đặt về 0.
