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
 * Cycle structure, full speed, based on "ZX Printer instructions"
 *
 * UNIT     |                  SINGLE STYLUS CYCLE                   | TOTAL
 *          |                                                        |
 *          |                  PAPER                  |     AIR      |
 *          |                                         |              |
 *          |MARGIN|            LINE           |MARGIN|              |
 * ---------+------+---------------------------+------+--------------|
 * mm       |  4   |             92            |  4   |      50      | 150
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

/*
 * Only used by timer handler.
 */
/** Clock step */
static volatile uint32_t zxprinter_clock_step;
/** Clock level */
static volatile uint32_t zxprinter_clock_level;
/** Single stylus cycle step number, zero to ZXPRINTER_CYCLE_STEPS */
static volatile uint32_t zxprinter_cycle_step;

/**
 * Check if a stylus step is on paper.
 *
 * @param step  The stylus step in the cycle.
 *
 * @return One if the stylus is on paper, zero otherwise.
 */
static unsigned int
zxprinter_cycle_is_on_paper(uint32_t step)
{
    return step < ZXPRINTER_CYCLE_PAPER_STEPS;
}

/**
 * Check if a stylus step is on the printable line.
 *
 * @param step  The stylus step in the cycle.
 *
 * @return One if the stylus is on the line, zero otherwise.
 */
static unsigned int
zxprinter_cycle_is_on_line(uint32_t step)
{
    return step >= ZXPRINTER_CYCLE_MARGIN_STEPS &&
           step < (ZXPRINTER_CYCLE_MARGIN_STEPS + ZXPRINTER_CYCLE_LINE_STEPS);
}

/**
 * Check if a stylus step is at the end of the cycle.
 *
 * @param step  The stylus step in the cycle.
 *
 * @return One if the stylus cycle is done, zero otherwise.
 */
static unsigned int
zxprinter_cycle_is_finished(uint32_t step)
{
    return step >= ZXPRINTER_CYCLE_STEPS;
}

/*
 * Read and written by timer handler, read by users.
 */
/** Number of lines input */
volatile uint32_t zxprinter_line_count_in;
/*
 * Read by timer handler, read and written by users.
 */
/** Number of lines output */
volatile uint32_t zxprinter_line_count_out;

/*
 * Written on init, used by timer handler.
 */
/** Line buffer */
static volatile uint8_t *zxprinter_line_buf;

void
zxprinter_tim_handler(void)
{
    /* Read the pins */
    uint16_t pins = zxprinter_gpio->idr;
    uint16_t motor_off = (pins >> ZXPRINTER_PIN_MOTOR_OFF) & 1U;
    uint16_t motor_slow = (pins >> ZXPRINTER_PIN_MOTOR_SLOW) & 1U;
    /* Determine current and next clock step and level */
    uint32_t clock_level = zxprinter_clock_level;
    uint32_t next_clock_step = zxprinter_clock_step + 1;
    uint32_t next_clock_level = (next_clock_step >> motor_slow) & 1U;

    /* If the clock is rising */
    if (next_clock_level > clock_level) {
        /* If the motor is not off */
        if (!motor_off) {
            uint32_t cycle_step, next_cycle_step;
            unsigned int on_paper, next_on_paper;
            unsigned int on_line, next_on_line;

            cycle_step = zxprinter_cycle_step;
            on_paper = zxprinter_cycle_is_on_paper(cycle_step);
            on_line = zxprinter_cycle_is_on_line(cycle_step);

            /* If we finished one stylus cycle */
            if (zxprinter_cycle_is_finished(cycle_step)) {
                next_cycle_step = 0;
            /* Else, we're in a stylus cycle */
            } else {
                next_cycle_step = cycle_step + 1;
            }

            next_on_paper = zxprinter_cycle_is_on_paper(next_cycle_step);
            next_on_line = zxprinter_cycle_is_on_line(next_cycle_step);

            /* If we're not waiting for the line output */
            if (!(next_on_line > on_line &&
                  zxprinter_line_count_out < zxprinter_line_count_in)) {
                /* Update outputs */
                zxprinter_gpio->odr = \
                    zxprinter_gpio->odr |
                    ((next_on_paper > on_paper) << ZXPRINTER_PIN_PAPER) |
                    (next_on_line << ZXPRINTER_PIN_ENCODER);

                /* Advance the cycle step */
                zxprinter_cycle_step = next_cycle_step;
                /* Advance the clock step */
                zxprinter_clock_step = next_clock_step;
                /* Change the clock level */
                zxprinter_clock_level = next_clock_level;
            }
        }
    /* Else, if the clock is falling */
    } else if (next_clock_level < clock_level) {
        /* If the stylus is on the line */
        if (zxprinter_cycle_is_on_line(zxprinter_cycle_step)) {
            uint32_t dot = (zxprinter_cycle_step -
                            ZXPRINTER_CYCLE_MARGIN_STEPS);
            uint8_t byte = dot >> 3;
            uint8_t bit = 7 - (dot & 0x7);
            uint8_t stylus = ((pins >> ZXPRINTER_PIN_STYLUS) & 1);
            /* Record dot state */
            zxprinter_line_buf[byte] =
                (zxprinter_line_buf[byte] & ~(1 << bit)) |
                (stylus << bit);
            /* Signal if the line is complete */
            if (dot + 1 >= ZXPRINTER_LINE_LEN) {
                zxprinter_line_count_in++;
            }
        }
        /* Advance the clock step */
        zxprinter_clock_step = next_clock_step;
        /* Change the clock level */
        zxprinter_clock_level = next_clock_level;
    /* Else, if the clock level is steady */
    } else {
        /* Advance the clock step */
        zxprinter_clock_step = next_clock_step;
    }

    /* Clear the interrupt flags */
    zxprinter_tim->sr = 0;
}

void
zxprinter_write_handler(void)
{
    uint16_t pins;
    /* Reset the "latches" ASAP */
    zxprinter_gpio->odr = zxprinter_gpio->odr &
                            ~((1U << ZXPRINTER_PIN_PAPER) |
                              (1U << ZXPRINTER_PIN_ENCODER));
    /* Read the pins */
    pins = zxprinter_gpio->idr;
    /* If motor is on */
    if (!((pins >> ZXPRINTER_PIN_MOTOR_OFF) & 1U)) {
        /* Start counting */
        zxprinter_tim->cr1 |= TIM_CR1_CEN_MASK;
    }
}

void
zxprinter_init(volatile struct gpio *gpio,
               volatile struct tim *tim,
               uint32_t ck_int,
               volatile uint8_t *line_buf)
{
    /*
     * Initialize the variables
     */
    zxprinter_gpio = gpio;
    zxprinter_tim = tim;
    zxprinter_line_buf = line_buf;
    /* Start in the air */
    zxprinter_clock_step = 0;
    zxprinter_clock_level = 0;
    zxprinter_cycle_step = ZXPRINTER_CYCLE_STEPS;
    /* No lines input */
    zxprinter_line_count_in = 0;
    /* No lines output */
    zxprinter_line_count_out = 0;

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
    /* Set the period */
    zxprinter_tim->arr = ZXPRINTER_CYCLE_STEP_PERIOD_US / 2;
    /* Ask to transfer data to shadow registers */
    zxprinter_tim->egr |= TIM_EGR_UG_MASK;
    /* Enable Capture/Compare 1 interrupt */
    zxprinter_tim->dier |= TIM_DIER_CC1IE_MASK;

    /* Set initial paper state */
    gpio_pin_set(zxprinter_gpio, ZXPRINTER_PIN_PAPER, 0);
    /* Set initial encoder state */
    gpio_pin_set(zxprinter_gpio, ZXPRINTER_PIN_ENCODER, 0);
    /* Signal printer interface is ready */
    gpio_pin_set(zxprinter_gpio, ZXPRINTER_PIN_READY, 1);
}
