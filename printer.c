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

/** The GPIO port used to output printer busy status */
static volatile struct gpio *printer_busy_gpio = NULL;

/** The pin on the GPIO port used to output printer busy status */
static volatile unsigned int printer_busy_pin;

/** Printer "busy" flag */
static volatile bool printer_busy = false;

/**
 * Set printer busy status.
 *
 * @param busy  The status to set.
 */
static void
printer_set_busy(bool busy)
{
    printer_busy = busy;
    gpio_pin_set(printer_busy_gpio, printer_busy_pin, printer_busy);
}

/**
 * Check if printer is busy.
 *
 * @return True if the printer is busy.
 */
static bool
printer_is_busy(void)
{
    return printer_busy;
}

void
printer_handler(void)
{
    if (printer_tim->sr & TIM_SR_CC1IF_MASK) {
        /* Free up the printer */
        printer_set_busy(false);
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
    while (printer_is_busy());
    /* Send the data */
    usart_transmit(printer_usart, ptr, len);
    /* Set printer busy, if requested */
    if (busy_ms_div_10 > 0) {
        /* Mark printer busy */
        printer_set_busy(true);
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
             uint32_t ck_int,
             volatile struct gpio *busy_gpio,
             unsigned int busy_pin)
{
    static const uint8_t init_cmd[] = {0x1B, 0x40};
    static const uint8_t config_cmd[] = {
        0x1B, 0x37,
        /* Max simultaneously heated dots, in units of 8 dots minus one */
        0x03,
        /* Heating time, in 10us units */
        0x70,
        /* Heating interval, in 10us units */
        0x0C};
    /*
     * Initialize the variables
     */
    printer_usart = usart;
    printer_tim = tim;
    printer_busy_gpio = busy_gpio;
    printer_busy_pin = busy_pin;

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
    printer_transmit(init_cmd, sizeof(init_cmd), 5000);
    printer_transmit(config_cmd, sizeof(config_cmd), 28);
}

void
printer_print_line(const uint8_t *line)
{
    static const uint8_t image_cmd[] = {0x12, 0x2A, 0x01, 0x30};
    printer_transmit(image_cmd, sizeof(image_cmd), 0);
    printer_transmit(line, 0x30, 67);
}
