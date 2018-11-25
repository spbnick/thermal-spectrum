/*
 * Thermal printer module
 */

#ifndef _PRINTER_H
#define _PRINTER_H

#include <usart.h>
#include <tim.h>
#include <stdint.h>

/**
 * Initialize the printer module, assuming it's called right after power-on.
 *
 * @param usart     The USART the printer is connected to. Must have line
 *                  parameters configured.
 * @param tim       The timer to use for timing communication with the printer.
 *                  Must be reset. Will be configured for operation.
 *                  The printer_handler() function should be arranged to be
 *                  called for the specified timer's interrupts.
 * @param ck_int    Frequency of the clock fed to the timer (CK_INT).
 */
extern void printer_init(volatile struct usart *usart,
                         volatile struct tim *tim,
                         uint32_t ck_int);

/**
 * Printer's timer interrupt handler.
 *
 * Must be called when an interrupt is triggered for the timer passed
 * previously to printer_init().
 */
extern void printer_handler(void);

/**
 * Print a line of pixels to the printer.
 *
 * @param line  An array of 48 bytes, where each bit stands for an output dot:
 *              zero for blank, one for black, for a total of 384 dots.
 */
extern void printer_print_line(const uint8_t *line);

#endif /* _PRINTER_H */
