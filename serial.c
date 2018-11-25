/*
 * Serial device
 */

#include "serial.h"

bool
serial_is_valid(const struct serial *serial)
{
    return serial != NULL &&
           serial->usart != NULL &&
           circular_buf_is_valid(&serial->tx_buf) &&
           circular_buf_is_valid(&serial->rx_buf);
}

void
serial_init(struct serial *serial, volatile struct usart *usart,
            void *tx_buf_ptr, size_t tx_buf_len,
            void *rx_buf_ptr, size_t rx_buf_len)
{
    assert(serial != NULL);
    assert(usart != NULL);
    assert(tx_buf_len > 0);
    assert(tx_buf_ptr != NULL || tx_buf_len == 1);
    assert(rx_buf_len > 0);
    assert(rx_buf_ptr != NULL || rx_buf_len == 1);

    serial->usart = usart;
    circular_buf_init(&serial->tx_buf, tx_buf_ptr, tx_buf_len);
    circular_buf_init(&serial->rx_buf, rx_buf_ptr, rx_buf_len);
    assert(serial_is_valid(serial));
}

size_t
serial_write(struct serial *serial, const void *ptr, size_t len)
{
    assert(serial_is_valid(serial));
    assert(ptr != NULL || len == 0);
    return circular_buf_write(&serial->tx_buf, ptr, len);
}

size_t
serial_read(struct serial *serial, void *ptr, size_t len)
{
    assert(serial_is_valid(serial));
    assert(ptr != NULL || len == 0);
    return circular_buf_read(&serial->rx_buf, ptr, len);
}

bool
serial_transmit(struct serial *serial)
{
    /* While there's still data to transmit */
    while (!circular_buf_is_empty(&serial->tx_buf)) {
        /* If USART is ready to transmit */
        if (serial->usart->sr & USART_SR_TXE_MASK) {
            uint8_t byte;
            /* Transfer a byte */
            circular_buf_read_byte(&serial->tx_buf, &byte);
            serial->usart->dr = byte;
        } else {
            /* Data still left */
            return true;
        }
    }
    /* Everything was transmitted */
    return false;
}

bool
serial_receive(struct serial *serial)
{
    /* While USART has data */
    while (serial->usart->sr & USART_SR_RXNE_MASK) {
        /* If we have no space to receive */
        if (circular_buf_is_full(&serial->rx_buf)) {
            /* We have more data, but no space */
            return true;
        } else {
            circular_buf_write_byte(&serial->rx_buf, serial->usart->dr);
        }
    }
    /* We received everything so far */
    return false;
}

