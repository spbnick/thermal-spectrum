/*
 * ZX Printer interface
 */

#include "zxprinter.h"
#include <stddef.h>

/** The interface's GPIO port */
static volatile struct gpio *zxprinter_gpio = NULL;

/** Motor's timer */
static volatile struct tim *zxprinter_tim = NULL;

/*
 * Cycle structure, based on ZX Printer instructions
 *
 * UNIT     |                  SINGLE STYLUS CYCLE                   | TOTAL
 *          |                                                        |
 *          |                  PAPER                  |     AIR      |
 *          |                                         |              |
 *          |MARGIN|            LINE           |MARGIN|              |
 * ---------+------+---------------------------+------+--------------|
 * mm       |  4   |             84            |  4   |      46      | 138
 * ms       |  1.4 |             29.2          |  1.4 |      16      |  48
 * steps    | 12   |            256            | 12   |     140      | 420
 *
 */

/* Number of cycle steps on a left/right paper margin */
#define ZXPRINTER_CYCLE_MARGIN_STEPS    12
/* Number of cycle steps on a printable line */
#define ZXPRINTER_CYCLE_LINE_STEPS      ZXPRINTER_LINE_LEN
/* Number of cycle steps on paper */
#define ZXPRINTER_CYCLE_PAPER_STEPS \
            (ZXPRINTER_CYCLE_MARGIN_STEPS +     \
             ZXPRINTER_CYCLE_LINE_STEPS +    \
             ZXPRINTER_CYCLE_MARGIN_STEPS)

/* Number of cycle steps in the air */
#define ZXPRINTER_CYCLE_AIR_STEPS 140

/* Number of cycle steps total, in a cycle of a single stylus */
#define ZXPRINTER_CYCLE_STEPS \
            (ZXPRINTER_CYCLE_AIR_STEPS + ZXPRINTER_CYCLE_PAPER_STEPS)

/* Duration of a cycle of a single stylus, ms */
#define ZXPRINTER_CYCLE_MS  48

/** Stylus cycle step period, microseconds */
#define ZXPRINTER_CYCLE_STEP_PERIOD_US \
            (ZXPRINTER_CYCLE_MS * 1000 / ZXPRINTER_CYCLE_STEPS)

/** Normal timer period, microseconds */
#define ZXPRINTER_TIM_NORMAL_PERIOD_US \
    (ZXPRINTER_CYCLE_STEP_PERIOD_US / 2)
/** Slow timer period, microseconds */
#define ZXPRINTER_TIM_SLOW_PERIOD_US \
    (ZXPRINTER_TIM_NORMAL_PERIOD_US * 2)

/*
 * Only used by timer handler.
 */
/** Clock state, zero or one */
static volatile uint8_t zxprinter_clock_state;
/** Single stylus cycle step number, zero to ZXPRINTER_CYCLE_STEPS */
static volatile uint32_t zxprinter_cycle_step;

/**
 * Check if the stylus is on paper.
 *
 * @return One if the stylus is on paper, zero otherwise.
 */
static unsigned int
zxprinter_cycle_is_on_paper(void)
{
    return zxprinter_cycle_step < ZXPRINTER_CYCLE_PAPER_STEPS;
}

/**
 * Check if the stylus is on the printable line.
 *
 * @return One if the stylus is on the line, zero otherwise.
 */
static unsigned int
zxprinter_cycle_is_on_line(void)
{
    uint32_t step = zxprinter_cycle_step;
    return step >= ZXPRINTER_CYCLE_MARGIN_STEPS &&
           step < (ZXPRINTER_CYCLE_MARGIN_STEPS + ZXPRINTER_CYCLE_LINE_STEPS);
}

/*
 * Read and written by timer handler, read by users.
 */
/** Number of lines printed */
volatile uint32_t zxprinter_line_count = 0;
/** Line buffer */
volatile uint8_t zxprinter_line_buf[ZXPRINTER_LINE_LEN / 8];

void
zxprinter_tim_handler(void)
{
    /* Update clock state */
    zxprinter_clock_state ^= 1;

    /* If the clock is rising */
    if (zxprinter_clock_state) {
        unsigned int on_paper_prev, on_paper;
        unsigned int on_line_prev, on_line;

        on_paper_prev = zxprinter_cycle_is_on_paper();
        on_line_prev = zxprinter_cycle_is_on_line();

        /* If we finished one stylus cycle */
        if (zxprinter_cycle_step >= ZXPRINTER_CYCLE_STEPS) {
            zxprinter_cycle_step = 0;
        /* Else, we're in a stylus cycle */
        } else {
            zxprinter_cycle_step++;
        }

        on_paper = zxprinter_cycle_is_on_paper();
        on_line = zxprinter_cycle_is_on_line();

        /* Update outputs */
        zxprinter_gpio->odr = \
            zxprinter_gpio->odr |
            ((on_paper > on_paper_prev) << ZXPRINTER_PIN_PAPER) |
            (on_line << ZXPRINTER_PIN_ENCODER);

        /* If we travelled out of the printable line */
        if (on_line < on_line_prev) {
            zxprinter_line_count++;
        }
    /* Else the clock is falling */
    } else {
        /* If the stylus is on the line */
        if (zxprinter_cycle_is_on_line()) {
            uint32_t dot = (zxprinter_cycle_step -
                            ZXPRINTER_CYCLE_MARGIN_STEPS);
            uint8_t byte = dot >> 3;
            uint8_t bit = 7 - (dot & 0x7);
            uint8_t stylus = ((zxprinter_gpio->idr >>
                               ZXPRINTER_PIN_STYLUS) & 1);
            /* Record dot state */
            zxprinter_line_buf[byte] =
                (zxprinter_line_buf[byte] & ~(1 << bit)) |
                (stylus << bit);
        }
    }

    /* Clear the interrupt flags */
    zxprinter_tim->sr = 0;
}

void
zxprinter_write_handler(void)
{
    /* Read the written value */
    uint16_t pins = zxprinter_gpio->idr;
    /* Reset the "latches" ASAP */
    zxprinter_gpio->odr = zxprinter_gpio->odr &
                            ~((1U << ZXPRINTER_PIN_PAPER) |
                              (1U << ZXPRINTER_PIN_ENCODER));

    /* If motor is turned off */
    if (pins & (1U << ZXPRINTER_PIN_MOTOR_OFF)) {
        /* Stop counting */
        zxprinter_tim->cr1 &= ~TIM_CR1_CEN_MASK;
    /* Else the motor is turned on */
    } else {
        uint16_t curr_arr = zxprinter_tim->arr;
        uint16_t new_arr;
        /* If motor is set to slow speed */
        if (pins & (1U << ZXPRINTER_PIN_MOTOR_SLOW)) {
            /* Set double timer period */
            new_arr = ZXPRINTER_TIM_SLOW_PERIOD_US;
        /* Else motor is set to normal speed */
        } else {
            /* Set normal timer period */
            new_arr = ZXPRINTER_TIM_NORMAL_PERIOD_US;
        }
        /* If "motor speed" changed */
        if (new_arr != curr_arr) {
            /* Update period */
            zxprinter_tim->arr = new_arr;
        }
        /* If "motor" was off */
        if (!(zxprinter_tim->cr1 & TIM_CR1_CEN_MASK)) {
            /* Ask to transfer data to shadow registers */
            zxprinter_tim->egr |= TIM_EGR_UG_MASK;
            /* Start counting */
            zxprinter_tim->cr1 |= TIM_CR1_CEN_MASK;
        }
    }
}

void
zxprinter_init(volatile struct gpio *gpio,
               volatile struct tim *tim,
               uint32_t ck_int)
{
    /*
     * Initialize the variables
     */
    zxprinter_gpio = gpio;
    zxprinter_tim = tim;
    /* Start in the air */
    zxprinter_clock_state = 0;
    zxprinter_cycle_step = ZXPRINTER_CYCLE_STEPS;

    /*
     * Setup the I/O pins
     */
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_READY,
                  GPIO_MODE_OUTPUT_50MHZ, GPIO_CNF_OUTPUT_GP_PUSH_PULL);
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_WRITE,
                  GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOATING);
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_STYLUS,
                  GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOATING);
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_PAPER,
                  GPIO_MODE_OUTPUT_50MHZ, GPIO_CNF_OUTPUT_GP_PUSH_PULL);
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_ENCODER,
                  GPIO_MODE_OUTPUT_50MHZ, GPIO_CNF_OUTPUT_GP_PUSH_PULL);
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_MOTOR_SLOW,
                  GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOATING);
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_MOTOR_OFF,
                  GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOATING);

    /*
     * Setup the timer for the motor emulation
     */
    /* Setup counting in microseconds */
    zxprinter_tim->psc = ck_int / 1000000;
    /* Select downcounting, enable auto-reload preload */
    zxprinter_tim->cr1 = (zxprinter_tim->cr1 & ~TIM_CR1_DIR_MASK) |
                         (TIM_CR1_DIR_VAL_DOWN << TIM_CR1_DIR_LSB) |
                         TIM_CR1_ARPE_MASK;
    /* Enable Capture/Compare 1 interrupt */
    zxprinter_tim->dier |= TIM_DIER_CC1IE_MASK;

    /* Set initial paper state */
    gpio_pin_set(zxprinter_gpio, ZXPRINTER_PIN_PAPER,
                 zxprinter_cycle_is_on_paper());
    /* Set initial encoder state */
    gpio_pin_set(zxprinter_gpio, ZXPRINTER_PIN_ENCODER,
                 zxprinter_clock_state && zxprinter_cycle_is_on_line());
    /* Signal printer interface is ready */
    gpio_pin_set(zxprinter_gpio, ZXPRINTER_PIN_READY, 1);
}
