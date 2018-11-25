/*
 * Circular buffer
 */

#include "circular_buf.h"

bool
circular_buf_is_valid(const struct circular_buf *buf)
{
    return buf != NULL &&
           buf->end != buf->ptr &&
           (buf->ptr != NULL || (buf->end - buf->ptr == 1)) &&
           buf->data_ptr >= buf->ptr &&
           buf->data_ptr < buf->end &&
           buf->data_end >= buf->ptr &&
           buf->data_end < buf->end;
}

void
circular_buf_init(struct circular_buf *buf, void *ptr, size_t len)
{
    assert(buf != NULL);
    /* This circular buffer has one byte storage overhead */
    assert(len > 0);
    /* Buffer with len == 1 has zero capacity */
    assert(ptr != NULL || len == 1);
    buf->ptr = ptr;
    buf->end = buf->ptr + len;
    buf->data_ptr = buf->ptr;
    buf->data_end = buf->ptr;
    assert(circular_buf_is_valid(buf));
}

bool
circular_buf_write_byte(struct circular_buf *buf, uint8_t byte)
{
    const uint8_t *data_ptr;
    uint8_t *data_end;

    assert(circular_buf_is_valid(buf));

    data_ptr = buf->data_ptr;
    data_end = buf->data_end;

    if (data_end >= data_ptr) {
        if (data_end < buf->end) {
            goto write;
        } else {
            data_end = buf->ptr;
        }
    }

    if (data_ptr - data_end <= 1) {
        return false;
    }

write:
    *data_end = byte;
    data_end++;
    /* TODO: Memory barrier? */
    buf->data_end = data_end;
    return true;
}

bool
circular_buf_read_byte(struct circular_buf *buf, uint8_t *pbyte)
{
    uint8_t *data_ptr;
    const uint8_t *data_end;

    assert(circular_buf_is_valid(buf));

    data_ptr = buf->data_ptr;
    data_end = buf->data_end;

    if (data_ptr > data_end) {
        if (data_ptr < buf->end) {
            goto read;
        } else {
            data_ptr = buf->ptr;
        }
    }

    if (data_ptr == data_end) {
        return false;
    }

read:
    if (pbyte != NULL) {
        *pbyte = *data_ptr;
    }
    data_ptr++;
    /* TODO: Memory barrier? */
    buf->data_ptr = data_ptr;
    return true;
}

size_t
circular_buf_write(struct circular_buf *buf, const void *ptr, size_t len)
{
    const uint8_t *data_ptr;
    uint8_t *data_end;
    const uint8_t *p = ptr;
    size_t l = len;

    assert(circular_buf_is_valid(buf));

    data_ptr = buf->data_ptr;
    data_end = buf->data_end;

    /* If we're ahead of the pointer */
    if (data_end >= data_ptr) {
        /* Fill-up until the end of the buffer */
        for (; l > 0 && data_end < buf->end;
             data_end++, p++, l--) {
            *data_end = *p;
        }
        /* If reached the end of the buffer */
        if (data_end >= buf->end) {
            /* Wrap around */
            data_end = buf->ptr;
        }
    }

    /* Fill-up until one byte before the pointer */
    for (; l > 0 && data_ptr - data_end > 1;
         data_end++, p++, l--) {
        *data_end = *p;
    }

    /* TODO: Memory barrier? */

    buf->data_end = data_end;
    return len - l;
}

size_t
circular_buf_read(struct circular_buf *buf, void *ptr, size_t len)
{
    uint8_t *data_ptr;
    const uint8_t *data_end;
    uint8_t *p = ptr;
    size_t l = len;

    assert(circular_buf_is_valid(buf));

    data_ptr = buf->data_ptr;
    data_end = buf->data_end;

    /* If we're beyond the data end */
    if (data_ptr > data_end) {
        /* Read until the end of the buffer */
        for (; l > 0 && data_ptr < buf->end;
             data_ptr++, p++, l--) {
            *p = *data_ptr;
        }
        /* If reached the end of the buffer */
        if (data_ptr >= buf->end) {
            /* Wrap around */
            data_ptr = buf->ptr;
        }
    }

    /* Read until the end of data  */
    for (; l > 0 && data_ptr < data_end;
         data_ptr++, p++, l--) {
        *p = *data_ptr;
    }

    /* TODO: Memory barrier? */

    buf->data_ptr = data_ptr;
    return len - l;
}

