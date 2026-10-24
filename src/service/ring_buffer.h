/*
 * ring_buffer.h - Bo dem vong 1 nha san xuat / 1 nha tieu thu (SPSC), khong khoa
 *
 * - ISR ghi, main doc (RX) hoac main ghi, ISR doc (TX) -> an toan khong can tat ngat
 *   vi head chi do ben ghi sua, tail chi do ben doc sua (truy cap 32-bit la nguyen tu tren M4).
 * - Kich thuoc bat buoc la luy thua cua 2 de dung phep AND thay cho phep chia.
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

typedef struct
{
    uint8_t           *buf;
    uint32_t           mask;      /* size - 1                         */
    volatile uint32_t  head;      /* Chi so ghi (tang mai, tu tran)   */
    volatile uint32_t  tail;      /* Chi so doc                       */
    volatile uint32_t  dropped;   /* So byte bi mat do day            */
    volatile uint32_t  peak;      /* Muc day cao nhat tung dat        */
} ring_buffer_t;

status_t RingBuf_Init(ring_buffer_t *rb, uint8_t *storage, uint32_t size);
bool     RingBuf_Push(ring_buffer_t *rb, uint8_t data);
bool     RingBuf_Pop(ring_buffer_t *rb, uint8_t *data);
uint32_t RingBuf_Count(const ring_buffer_t *rb);
uint32_t RingBuf_Free(const ring_buffer_t *rb);

#endif /* RING_BUFFER_H */
