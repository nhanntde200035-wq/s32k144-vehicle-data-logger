/*
 * ring_buffer.c
 */
#include <stddef.h>
#include "ring_buffer.h"

static bool IsPowerOfTwo(uint32_t x)
{
    return (x != 0u) && ((x & (x - 1u)) == 0u);
}

status_t RingBuf_Init(ring_buffer_t *rb, uint8_t *storage, uint32_t size)
{
    if ((rb == NULL) || (storage == NULL) || (!IsPowerOfTwo(size)))
    {
        return STATUS_ERR_PARAM;
    }
    rb->buf     = storage;
    rb->mask    = size - 1u;
    rb->head    = 0u;
    rb->tail    = 0u;
    rb->dropped = 0u;
    rb->peak    = 0u;
    return STATUS_OK;
}

uint32_t RingBuf_Count(const ring_buffer_t *rb)
{
    return rb->head - rb->tail;           /* Dung ca khi head/tail tu tran */
}

uint32_t RingBuf_Free(const ring_buffer_t *rb)
{
    return (rb->mask + 1u) - RingBuf_Count(rb);
}

bool RingBuf_Push(ring_buffer_t *rb, uint8_t data)
{
    uint32_t head  = rb->head;
    uint32_t count = head - rb->tail;

    if (count > rb->mask)                 /* Day */
    {
        rb->dropped++;
        return false;
    }
    rb->buf[head & rb->mask] = data;
    rb->head = head + 1u;                 /* Cap nhat SAU khi ghi du lieu */

    if ((count + 1u) > rb->peak)
    {
        rb->peak = count + 1u;
    }
    return true;
}

bool RingBuf_Pop(ring_buffer_t *rb, uint8_t *data)
{
    uint32_t tail = rb->tail;

    if (tail == rb->head)                 /* Rong */
    {
        return false;
    }
    *data = rb->buf[tail & rb->mask];
    rb->tail = tail + 1u;                 /* Cap nhat SAU khi doc du lieu */
    return true;
}
