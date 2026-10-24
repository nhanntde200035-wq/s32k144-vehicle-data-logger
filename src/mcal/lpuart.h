/*
 * lpuart.h - Driver LPUART muc thanh ghi, nhieu cong, RX/TX bang ngat + ring buffer
 *
 * Chua khoa: moi chuoi phat ra tat ca cong ung vien.
 * Byte dau tien nhan duoc tren cong nao -> console khoa vao cong do.
 */
#ifndef LPUART_H
#define LPUART_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

#define UART_PORT_COUNT   2u
#define UART_PORT_NONE    (-1)

typedef struct
{
    uint32_t rx_bytes;        /* Tong byte nhan (cong da khoa)      */
    uint32_t tx_bytes;        /* Byte da phat ra cong da khoa       */
    uint32_t rx_dropped;      /* Mat do ring RX day                 */
    uint32_t tx_dropped;      /* Mat do ring TX day                 */
    uint32_t err_overrun;     /* Phan cung: OR (CPU doc khong kip)  */
    uint32_t err_framing;     /* Phan cung: FE                      */
    uint32_t err_noise;       /* Phan cung: NF                      */
    uint32_t rx_peak;
    uint32_t tx_peak;
    uint32_t baud_actual;     /* Baud thuc te sau khi chia          */
} uart_stats_t;

status_t    Uart_Init(uint32_t baud);
uint32_t    Uart_Write(const uint8_t *data, uint32_t len);
uint32_t    Uart_WriteTo(uint32_t idx, const uint8_t *data, uint32_t len);
uint32_t    Uart_Puts(const char *s);
uint32_t    Uart_Printf(const char *fmt, ...);
uint32_t    Uart_PrintfTo(uint32_t idx, const char *fmt, ...);
bool        Uart_ReadByte(uint8_t *data);
uint32_t    Uart_TxFree(void);
uint32_t    Uart_GetPortRx(uint32_t idx);   /* Tong byte nhan tren cong idx */
bool        Uart_IsLocked(void);
const char *Uart_GetPortName(uint32_t idx);
const char *Uart_GetActiveName(void);
void        Uart_GetStats(uart_stats_t *st);
void        Uart_ClearStats(void);

#endif /* LPUART_H */
