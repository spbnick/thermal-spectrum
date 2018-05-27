/*
 * A ZX-printer interface for a thermal printer module
 */
#include <init.h>
#include <usart.h>
#include <gpio.h>
#include <rcc.h>
#include <misc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Serial circular transmit buffer array */
static volatile uint8_t SERIAL_TX_BUF_ARR[1024];
/** End of serial circular transmit buffer */
static const volatile uint8_t *SERIAL_TX_BUF_END =
                                SERIAL_TX_BUF_ARR +
                                ARRAY_SIZE(SERIAL_TX_BUF_ARR);
/** Start of valid data in serial circular transmit buffer */
static volatile uint8_t *SERIAL_TX_BUF_DATA_PTR = SERIAL_TX_BUF_ARR;
/** End of valid data in serial circular transmit buffer */
static volatile uint8_t *SERIAL_TX_BUF_DATA_END = SERIAL_TX_BUF_ARR;

/**
 * Attempt to write data to the serial transmit buffer.
 *
 * @param ptr   The pointer to the data to write to the buffer.
 * @param len   The length of the data to write to the buffer, bytes.
 *
 * @return Amount of data which fit into the buffer, bytes.
 */
static size_t
serial_tx_buf_write(const uint8_t *ptr, size_t len)
{
    volatile uint8_t *data_ptr = SERIAL_TX_BUF_DATA_PTR;
    volatile uint8_t *data_end = SERIAL_TX_BUF_DATA_END;
    const uint8_t *p = ptr;

    /* If we're ahead of the pointer */
    if (data_end >= data_ptr) {
        /* Fill-up until the end of the buffer */
        for (; len > 0 && data_end < SERIAL_TX_BUF_END;
             data_end++, p++, len--) {
            *data_end = *p;
        }
        /* If reached the end of the buffer */
        if (data_end >= SERIAL_TX_BUF_END) {
            /* Wrap around */
            data_end = SERIAL_TX_BUF_ARR;
        }
    }

    /* Fill-up until one byte before the pointer */
    for (; len > 0 && data_ptr - data_end > 1;
         data_end++, p++, len--) {
        *data_end = *p;
    }

    SERIAL_TX_BUF_DATA_END = data_end;
    return len;
}

/**
 * Attempt to read a byte from the serial transmit buffer.
 *
 * @param pbyte Location for the retrieved byte.
 *              Not modified, if the buffer was empty.
 *
 * @return True if the byte was read, false if the buffer was empty.
 */
static bool
serial_tx_buf_read_byte(uint8_t *pbyte)
{
    volatile uint8_t *data_ptr = SERIAL_TX_BUF_DATA_PTR;
    volatile uint8_t *data_end = SERIAL_TX_BUF_DATA_END;

    if (data_ptr >= data_end) {
        return false;
    }

    *pbyte = *data_ptr;
    data_ptr++;
    if (data_ptr >= SERIAL_TX_BUF_END) {
        data_ptr = SERIAL_TX_BUF_ARR;
    }
    SERIAL_TX_BUF_DATA_PTR = data_ptr;

    return true;
}

int
main(void)
{
    volatile struct usart *usart = USART1;
    uint8_t b;

    /* Basic init */
    init();

    /*
     * Enable clocks
     */
    /* Enable APB2 clock to I/O port A */
    RCC->apb2enr |= RCC_APB2ENR_IOPAEN_MASK;

    /*
     * Configure pins
     */
    /* Configure TX pin (PA9) */
    gpio_pin_conf(GPIO_A, 9,
                  GPIO_MODE_OUTPUT_50MHZ,
                  GPIO_CNF_OUTPUT_AF_PUSH_PULL);
    /* Configure RX pin (PA10) */
    gpio_pin_conf(GPIO_A, 10,
                  GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOATING);

    /*
     * Configure USART
     */
    /* Enable clock to USART1 */
    RCC->apb2enr |= RCC_APB2ENR_USART1EN_MASK;

    /* Enable USART, leave the default mode of 8N1 */
    usart->cr1 |= USART_CR1_UE_MASK;

    /* Set baud rate of 115200 based on PCLK1 at 36MHz */
    usart->brr = usart_brr_val(72 * 1000 * 1000, 115200);

    /* Enable receiver and transmitter */
    usart->cr1 |= USART_CR1_RE_MASK | USART_CR1_TE_MASK;

    /*
     * Write test serial data
     */
    const uint8_t message[] = "Hello, world!\r\n";
    serial_tx_buf_write(message, sizeof(message) - 1);

    /*
     * Transmit serial data
     */
    while (1) {
        while (serial_tx_buf_read_byte(&b)) {
            /* Wait for transmit register to empty */
            while (!(usart->sr & USART_SR_TXE_MASK));
            /* Write the byte */
            usart->dr = b;
        }
    }
}
