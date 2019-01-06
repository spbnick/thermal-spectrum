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
    /* Reads high when a dot could be/could have been printed */
    ZXPRINTER_PIN_ENCODER       = 7,
    /* Written high for lower motor speed, low for normal speed */
    ZXPRINTER_PIN_MOTOR_SLOW    = 8,
    /* Written high to turn the motor off, low to turn it on */
    ZXPRINTER_PIN_MOTOR_OFF     = 9,
    /* Reads low when printer is present, high when not */
    ZXPRINTER_PIN_NOT_PRESENT   = 12,
    /* Reads high when stylus detected paper, written high to print a dot */
    ZXPRINTER_PIN_PAPER_STYLUS  = 13,
    /* High when the interface is accessed */
    ZXPRINTER_PIN_SELECT        = 14,
    /* High when the interface is being read from, low when written to */
    ZXPRINTER_PIN_RD            = 15,
};

/** Number of dots on a line */
#define ZXPRINTER_LINE_LEN  256

/** Line buffer */
extern volatile uint8_t zxprinter_line_buf[ZXPRINTER_LINE_LEN / 8];
/** Number of lines printed */
extern volatile uint32_t zxprinter_line_count;

/**
 * Initialize the ZX Printer interface.
 *
 * @param gpio      The GPIO port the interface is connected to.
 *                  The port should have signals assigned to pins as defined
 *                  by enum zxprinter_pin. The zxprinter_select_handler()
 *                  function should be arranged to be called for both edges of
 *                  the SELECT signal.
 * @param tim       The timer to use for encoder disc emulation.
 *                  Must be reset. Will be configured for operation.
 *                  The zxprinter_tip_handler() function should be arranged to
 *                  be called for the specified timer's interrupts.
 * @param ck_int    Frequency of the clock fed to the timer (CK_INT).
 */
extern void zxprinter_init(volatile struct gpio *gpio,
                           volatile struct tim *tim,
                           uint32_t ck_int);

/**
 * ZX Printer interface timer interrupt handler.
 *
 * Must be called when an interrupt is triggered for the timer passed
 * previously to zxprinter_init().
 */
extern void zxprinter_tim_handler(void);

/**
 * ZX Printer interface SELECT line change handler.
 *
 * Must be called on every SELECT line change, which is pin
 * ZXPRINTER_PIN_SELECT of the GPIO port previously passed to
 * zxprinter_init().
 */
extern void zxprinter_select_handler(void);

#endif /* _ZXPRINTER_H */
