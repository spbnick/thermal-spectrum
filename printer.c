/*
 * Thermal printer module
 */
#include "printer.h"
#include <gpio.h>
#include <stddef.h>
#include <stdbool.h>

/** The USART connected to the printer */
static volatile struct usart *printer_usart = NULL;

/** The timer used to trigger printer communication */
static volatile struct tim *printer_tim = NULL;

/** Printer "busy" flag */
static volatile bool printer_busy = false;

void
printer_handler(void)
{
    if (printer_tim->sr & TIM_SR_CC1IF_MASK) {
        /* Free up the printer */
        printer_busy = false;
    }
    /* Clear the interrupt flags */
    printer_tim->sr = 0;
}

/**
 * Transmit data to the printer, putting it into busy state for the
 * specified amount of time, afterwards.
 *
 * @param ptr               Pointer to the data to transmit.
 * @param len               Length of the data to transmit, bytes.
 * @param busy_ms_div_10    The time period to consider the printer busy after
 *                          transmitting the buffer, tenths of millisecond.
 */
static void
printer_transmit(const void *ptr, size_t len, uint16_t busy_ms_div_10)
{
    /* Wait for the printer to free up */
    while (printer_busy);
    /* Send the data */
    usart_transmit(printer_usart, ptr, len);
    /* Set printer busy, if requested */
    if (busy_ms_div_10 > 0) {
        /* Mark printer busy */
        printer_busy = true;
        /* Set time to count */
        printer_tim->arr = busy_ms_div_10;
        /* Generate an update event to transfer data to shadow registers */
        printer_tim->egr |= TIM_EGR_UG_MASK;
        /* Start counting */
        printer_tim->cr1 |= TIM_CR1_CEN_MASK | TIM_CR1_OPM_MASK;
    }
}

void
printer_init(volatile struct usart *usart,
             volatile struct tim *tim,
             uint32_t ck_int)
{
    static const uint8_t config_cmd[] = {0x1B, 0x37, 0x03, 0x70, 0x0C};
    static const uint8_t reset_cmd[] = {0x1B, 0x40};
    /*
     * Initialize the variables
     */
    printer_usart = usart;
    printer_tim = tim;

    /*
     * Configure the timer
     */
    /* Setup counting in 1/10th of milliseconds */
    printer_tim->psc = ck_int / 10000;
    /* Select downcounting, enable auto-reload preload */
    printer_tim->cr1 = (printer_tim->cr1 & ~TIM_CR1_DIR_MASK) |
                       (TIM_CR1_DIR_VAL_DOWN << TIM_CR1_DIR_LSB) |
                       TIM_CR1_ARPE_MASK;
    /* Enable Capture/Compare 1 interrupt */
    printer_tim->dier |= TIM_DIER_CC1IE_MASK;

    /*
     * Initialize the printer after a power-on
     */
    printer_transmit(NULL, 0, 30000);
    printer_transmit(config_cmd, sizeof(config_cmd), 28);
    printer_transmit(reset_cmd, sizeof(reset_cmd), 354);
}

void
printer_print_line(const uint8_t *line)
{
    static const uint8_t image_cmd[] = {0x12, 0x2A, 0x01, 0x30};
    printer_transmit(image_cmd, sizeof(image_cmd), 0);
    printer_transmit(line, 0x30, 67);
}
