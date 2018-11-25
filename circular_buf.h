/*
 * Circular FIFO buffer.
 */

#ifndef _CIRCULAR_BUF_H
#define _CIRCULAR_BUF_H

#include <misc.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Writing adds data to the end, reading removes from the start.
 *
 * State examples:
 *
 *  [ ][ ][ ][ ][ ][ ][ ][ ]
 *            P
 *            E
 *
 *  [ ][ ][ ][*][ ][ ][ ][ ]
 *            P
 *               E
 *
 *  [ ][ ][ ][*][*][ ][ ][ ]
 *            P
 *                  E
 *
 *  [ ][ ][ ][*][*][*][*][ ]
 *            P
 *                        E
 *
 *  [ ][ ][ ][*][*][*][*][*]
 *            P
 *   E
 *
 *  [*][ ][ ][*][*][*][*][*]
 *            P
 *      E
 *
 *  [*][*][ ][*][*][*][*][*]
 *            P
 *         E
 *
 *  [ ][ ][ ][ ][ ][ ][ ][ ]    P - E == 0
 *            P
 *            E
 *
 *  [*][*][*][*][*][*][ ][*]    P - E == 1
 *                        P
 *                     E
 *
 *  [*][*][*][*][*][*][*][ ]    P - E == - (L - 1)
 *   P
 *                        E
 *
 *  [ ][ ][ ][ ][ ][ ][ ][*]
 *                        P
 *   E
 *
 * Here, "[ ]" is empty element, "[*]" is full element, "P" is data pointer,
 * "E" is data end, and "L" is buffer length.
 */

/** Circular buffer */
struct circular_buf {
    /** Buffer array */
    uint8_t *ptr;
    /** End of buffer */
    const uint8_t *end;
    /** Start of valid data */
    uint8_t *data_ptr;
    /** End of valid data */
    uint8_t *data_end;
};

/**
 * Initialize a circular buffer.
 *
 * @param buf   The buffer to initialize.
 * @param ptr   Pointer to the data buffer to use for storage.
 * @param len   Length of the data buffer to use for storage.
 */
extern void circular_buf_init(struct circular_buf *buf,
                              void *ptr, size_t len);

/**
 * Check if a circular buffer is valid.
 *
 * @param buf   The buffer to check.
 *
 * @return True if the buffer is valid, false otherwise.
 */
extern bool circular_buf_is_valid(const struct circular_buf *buf);

/**
 * Check if a circular buffer is full.
 *
 * @param buf   The buffer to check.
 *
 * @return True if the buffer is full, false otherwise.
 */
static inline bool
circular_buf_is_full(const struct circular_buf *buf)
{
    int diff;
    assert(circular_buf_is_valid(buf));
    diff = buf->data_end - buf->data_ptr;
    return diff == -1 || diff == (buf->end - buf->ptr - 1);
}

/**
 * Check if a circular buffer is empty.
 *
 * @param buf   The buffer to check.
 *
 * @return True if the buffer is empty, false otherwise.
 */
static inline bool
circular_buf_is_empty(const struct circular_buf *buf)
{
    assert(circular_buf_is_valid(buf));
    return buf->data_ptr == buf->data_end;
}

/**
 * Get length of data stored in a circular buffer.
 *
 * @param buf   The buffer to get the data length for.
 *
 * @return The buffer data length.
 */
static inline size_t
circular_buf_data_len(const struct circular_buf *buf)
{
    int diff;
    assert(circular_buf_is_valid(buf));
    diff = buf->data_end - buf->data_ptr;
    return diff >= 0 ? diff
                     : (buf->end - buf->ptr) + diff;
}

/**
 * Write a byte to a circular buffer.
 *
 * @param buf   The circular buffer to write to.
 * @param byte  The byte to write.
 *
 * @return True if the byte fit the buffer (and was written), false otherwise.
 */
extern bool circular_buf_write_byte(struct circular_buf *buf, uint8_t byte);

/**
 * Read a byte from a circular buffer.
 *
 * @param buf   The circular buffer to read from.
 * @param pbyte The location for the byte read.
 *              Can be NULL to have the byte discarded.
 *
 * @return True if the byte was available (and was output), false otherwise.
 */
extern bool circular_buf_read_byte(struct circular_buf *buf, uint8_t *pbyte);

/**
 * Write to a circular buffer.
 *
 * @param buf   The circular buffer to write to.
 * @param ptr   Pointer to the data to write.
 * @param len   Length of the data to write.
 *
 * @return Number of bytes that fit the buffer.
 */
extern size_t circular_buf_write(struct circular_buf *buf,
                                 const void *ptr, size_t len);

/**
 * Read from a circular buffer.
 *
 * @param buf   The circular buffer to read from.
 * @param ptr   Location of the read data buffer.
 * @param len   Length of the read data buffer.
 *
 * @return Number of bytes that were read. Could be less than len.
 */
extern size_t circular_buf_read(struct circular_buf *buf,
                                void *ptr, size_t len);

#endif /* _CIRCULAR_BUF_H */
