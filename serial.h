/*
 * Serial device
 */

#ifndef _SERIAL_H
#define _SERIAL_H

#include "circular_buf.h"
#include <usart.h>

/** Serial device */
struct serial {
    /* Underlying USART device */
    volatile struct usart *usart;

    /** Transmit circular buffer array */
    struct circular_buf tx_buf;

    /** Receive circular buffer array */
    struct circular_buf rx_buf;
};

/**
 * Check if a serial device is valid.
 *
 * @param serial    The serial device to check.
 *
 * @return True if the device is valid, false otherwise.
 */
extern bool serial_is_valid(const struct serial *serial);

/**
 * Initialize a serial device.
 *
 * @param serial        The serial device to initialize. Cannot be NULL.
 * @param usart         The USART to use for the serial device. Must be
 *                      initialized before "serial_transmit" or
 *                      "serial_receive" are called. Cannot be NULL.
 * @param tx_buf_ptr    Pointer to the transmit buffer. Cannot be NULL unless
 *                      "tx_buf_len" is one.
 * @param tx_buf_len    Length of the transmit buffer. Must be at least one.
 * @param rx_buf_ptr    Pointer to the receive buffer. Cannot be NULL unless
 *                      "rx_buf_len" is one.
 * @param rx_buf_len    Length of the receive buffer. Must be at least one.
 */
extern void serial_init(struct serial *serial, volatile struct usart *usart,
                        void *tx_buf_ptr, size_t tx_buf_len,
                        void *rx_buf_ptr, size_t rx_buf_len);

/**
 * Write to a serial device's transmit buffer.
 *
 * @param serial    The serial device to write to.
 * @param ptr       Pointer to the buffer to write.
 * @param len       Length of the buffer to write.
 *
 * @return Number of bytes that fit the device buffer.
 */
extern size_t serial_write(struct serial *serial,
                           const void *ptr, size_t len);

/**
 * Read from a serial device's receive buffer.
 *
 * @param serial    The serial device to read from.
 * @param ptr       Pointer to the buffer to receive the data.
 * @param len       Length of the buffer to receive the data.
 *
 * @return Number of bytes actually read.
 */
extern size_t serial_read(struct serial *serial,
                          void *ptr, size_t len);

/**
 * Transmit serial device's data while USART is ready and there's data in the
 * buffer.
 *
 * @param serial    The serial device to transmit the data for.
 *
 * @return True if there's still data to transmit in the buffer and USART is
 *         not ready anymore, false if all the data was transmitted.
 */
extern bool serial_transmit(struct serial *serial);

/**
 * Receive serial device's data while it's available in the USART and there's
 * space in the buffer.
 *
 * @param serial    The serial device to receive the data for.
 *
 * @return True if more data is available from USART, but there's no more
 *         space in the receive buffer, false if USART has no more data.
 */
extern bool serial_receive(struct serial *serial);

#endif /* _SERIAL_H */
