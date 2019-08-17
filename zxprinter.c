/*
 * ZX Printer interface
 */

#include "zxprinter.h"
#include <stddef.h>

/** The interface's GPIO port */
static volatile struct gpio *zxprinter_gpio = NULL;

/** Motor's timer */
static volatile struct tim *zxprinter_tim = NULL;

/** Encoder pulse period, microseconds */
#define ZXPRINTER_ENCODER_PERIOD_US 60
/** Normal encoder timer period, microseconds */
#define ZXPRINTER_ENCODER_TIM_NORMAL_PERIOD_US \
    (ZXPRINTER_ENCODER_PERIOD_US / 2)
/** Slow encoder timer period, microseconds */
#define ZXPRINTER_ENCODER_TIM_SLOW_PERIOD_US \
    (ZXPRINTER_ENCODER_TIM_NORMAL_PERIOD_US * 2)
/** Number of encoder pulses with a stylus on paper */
#define ZXPRINTER_ENCODER_PULSES_ON_PAPER   ZXPRINTER_LINE_LEN
/** Number of encoder pulses with a stylus off paper */
#define ZXPRINTER_ENCODER_PULSES_OFF_PAPER  (ZXPRINTER_LINE_LEN / 2)
/** Total number of encoder pulses in a loop */
#define ZXPRINTER_ENCODER_PULSES_TOTAL \
            (ZXPRINTER_ENCODER_PULSES_ON_PAPER +  \
             ZXPRINTER_ENCODER_PULSES_OFF_PAPER)

/*
 * Only used by timer handler.
 */
/** Encoder state, zero or one */
static volatile uint8_t zxprinter_encoder_state;
/** Encoder pulse number, zero to ZXPRINTER_ENCODER_PULSES_TOTAL - 1 */
static volatile uint32_t zxprinter_encoder_pulse_num;
/** Paper-touching state, zero or one */
static volatile uint8_t zxprinter_paper_state;

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
    /* Update encoder state */
    zxprinter_encoder_state ^= 1;

    /* If we're turning encoder signal on */
    if (zxprinter_encoder_state) {
        /* If we finished one stylus cycle */
        if (zxprinter_encoder_pulse_num >= ZXPRINTER_ENCODER_PULSES_TOTAL) {
            zxprinter_encoder_pulse_num = 0;
            zxprinter_paper_state = 1;
        /* Else, we're in a stylus cycle */
        } else {
            zxprinter_encoder_pulse_num++;
            /* If stylus travelled off paper */
            if (zxprinter_encoder_pulse_num >=
                ZXPRINTER_ENCODER_PULSES_ON_PAPER) {
                zxprinter_paper_state = 0;
                zxprinter_line_count++;
            }
        }
    /* Else we're turning it off */
    } else {
        /* If the stylus is on paper */
        if (zxprinter_paper_state) {
            uint8_t byte = zxprinter_encoder_pulse_num >> 3;
            uint8_t bit = 7 - (zxprinter_encoder_pulse_num & 0x7);
            uint8_t dot = ((zxprinter_gpio->idr >>
                            ZXPRINTER_PIN_DOT_LATCH) & 1);
            /* Record dot state */
            zxprinter_line_buf[byte] =
                (zxprinter_line_buf[byte] & ~(1 << bit)) |
                (dot << bit);
        }
    }

    /* Update outputs */
    zxprinter_gpio->odr = (zxprinter_gpio->odr &
                           ~((1U << ZXPRINTER_PIN_PAPER) |
                             (1U << ZXPRINTER_PIN_ENCODER))) |
                          (zxprinter_paper_state << ZXPRINTER_PIN_PAPER) |
                          (zxprinter_encoder_state << ZXPRINTER_PIN_ENCODER);

    /* Clear the interrupt flags */
    zxprinter_tim->sr = 0;
}

void
zxprinter_write_handler(void)
{
    /* Read the written value */
    uint16_t pins = zxprinter_gpio->idr;

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
            new_arr = ZXPRINTER_ENCODER_TIM_SLOW_PERIOD_US;
        /* Else motor is set to normal speed */
        } else {
            /* Set normal timer period */
            new_arr = ZXPRINTER_ENCODER_TIM_NORMAL_PERIOD_US;
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
    zxprinter_encoder_pulse_num = 0;
    zxprinter_encoder_state = 1;
    zxprinter_paper_state = 1;

    /*
     * Setup the I/O pins
     */
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_READY,
                  GPIO_MODE_OUTPUT_50MHZ, GPIO_CNF_OUTPUT_GP_PUSH_PULL);
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_WRITE,
                  GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOATING);
    gpio_pin_conf(zxprinter_gpio, ZXPRINTER_PIN_DOT_LATCH,
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
                 zxprinter_paper_state);
    /* Set initial encoder state */
    gpio_pin_set(zxprinter_gpio, ZXPRINTER_PIN_ENCODER,
                 zxprinter_encoder_state);
    /* Signal printer interface is ready */
    gpio_pin_set(zxprinter_gpio, ZXPRINTER_PIN_READY, 1);
}
