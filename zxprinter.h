/*
 * ZX Printer interface
 */

#ifndef _ZXPRINTER_H
#define _ZXPRINTER_H

#include <gpio.h>
#include <tim.h>
#include <stdint.h>

/** ZX Printer interface GPIO port pin number */
enum zxprinter_pin {
    /* Pulled high, when printer is ready */
    ZXPRINTER_PIN_READY         = 7,
    /* Reads high when printer device is being written to */
    ZXPRINTER_PIN_WRITE         = 8,
    /* Reads high, if the power should be applied to the stylus */
    ZXPRINTER_PIN_STYLUS        = 9,
    /* Written high when a stylus is on paper, low otherwise */
    ZXPRINTER_PIN_PAPER         = 12,
    /* Written high when a dot could be printed, low otherwise */
    ZXPRINTER_PIN_ENCODER       = 13,
    /* Reads high for lower motor speed, low for normal speed */
    ZXPRINTER_PIN_MOTOR_SLOW    = 14,
    /* Reads high when motor must be off, low when it must be on */
    ZXPRINTER_PIN_MOTOR_OFF     = 15,
};

/** Number of dots on a line */
#define ZXPRINTER_LINE_LEN  256

/** Number of lines printed */
extern volatile uint32_t zxprinter_line_count;

/**
 * Initialize the ZX Printer interface.
 *
 * @param gpio      The GPIO port the interface is connected to.
 *                  The port should have signals assigned to pins as defined
 *                  by enum zxprinter_pin. The zxprinter_write_handler()
 *                  function should be arranged to be called for the rising
 *                  edge of the WRITE signal, after zxprinter_init() completed.
 * @param tim       The timer to use for encoder disc emulation.
 *                  Must be reset. Will be configured for operation.
 *                  The zxprinter_tim_handler() function should be arranged to
 *                  be called for the specified timer's interrupts, after
 *                  zxprinter_init() completed.
 * @param ck_int    Frequency of the clock fed to the timer (CK_INT).
 * @param line_buf  Pointer to the buffer to output input lines to.
 */
extern void zxprinter_init(volatile struct gpio *gpio,
                           volatile struct tim *tim,
                           uint32_t ck_int,
                           volatile uint8_t *line_buf);

/**
 * ZX Printer interface timer interrupt handler.
 *
 * Must be called when an interrupt is triggered for the timer passed
 * previously to zxprinter_init().
 */
extern void zxprinter_tim_handler(void);

/**
 * ZX Printer interface WRITE line raising handler.
 *
 * Must be called on the rising edge of the WRITE line, which is pin
 * ZXPRINTER_PIN_WRITE of the GPIO port previously passed to zxprinter_init().
 */
extern void zxprinter_write_handler(void);

#endif /* _ZXPRINTER_H */
