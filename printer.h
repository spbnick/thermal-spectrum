/*
 * Thermal printer module
 */

#ifndef _PRINTER_H
#define _PRINTER_H

#include <gpio.h>
#include <usart.h>
#include <tim.h>
#include <adc.h>
#include <stdint.h>

/**
 * Initialize the printer module, assuming it's called right after power-on.
 *
 * @param usart     The USART the printer is connected to. Must have line
 *                  parameters configured.
 * @param adc       The ADC to use for measuring the printer's current
 *                  consumption, for determining its busy status.
 *                  Must be calibrated and powered down.
 * @param adc_chan  The number of the ADC channel to use for measuring the
 *                  printer's current consumption.
 * @param tim       The timer to use for timing communication with the printer.
 *                  Must be reset. Will be configured for operation.
 *                  The printer_tim_handler() function should be arranged to
 *                  be called for the specified timer's interrupts.
 * @param ck_int    Frequency of the clock fed to the timer (CK_INT).
 * @param busy_gpio The GPIO port used to output the printer busy status.
 * @param busy_pin  The pin on the GPIO port used to output the printer busy
 *                  status.
 */
extern void printer_init(volatile struct usart *usart,
                         volatile struct adc *adc,
                         unsigned int adc_chan,
                         volatile struct tim *tim,
                         uint32_t ck_int,
                         volatile struct gpio *busy_gpio,
                         unsigned int busy_pin);

/**
 * Printer's timer interrupt handler.
 *
 * Must be called when an interrupt is triggered for the timer passed
 * previously to printer_init().
 */
extern void printer_tim_handler(void);

/**
 * Printer's ADC interrupt handler.
 *
 * Must be called when an interrupt is triggered for the ADC passed
 * previously to printer_init().
 */
extern void printer_adc_handler(void);

/**
 * Print a line of pixels to the printer.
 *
 * @param line  An array of 48 bytes, where each bit stands for an output dot:
 *              zero for blank, one for black, for a total of 384 dots.
 */
extern void printer_print_line(const uint8_t *line);

#endif /* _PRINTER_H */
