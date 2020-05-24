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

/** True if the timer is running, false otherwise */
static volatile bool printer_tim_running;

/**
 * Time to consider printer busy after last busy current was seen,
 * tenths of milliseconds.
 */
static const uint16_t printer_busy_time_ms_div_10 = 1;

/** The GPIO port used to output printer busy status */
static volatile struct gpio *printer_busy_gpio = NULL;

/** The pin on the GPIO port used to output printer busy status */
static volatile unsigned int printer_busy_pin;

/** Printer "busy" flag */
static volatile bool printer_busy = true;

/** The ADC used to read printer current consumption */
static volatile struct adc *printer_adc = NULL;

/** The ADC channel used to read printer current consumption */
static volatile unsigned int printer_adc_chan;

/** Maximum printer idle current */
static volatile unsigned int printer_adc_current_idle = 0;

/** Maximum printer feed current */
static volatile unsigned int printer_adc_current_feed = 0;

/** Printer state. Modified by printer_init() */
static volatile enum {
    /* Initializing */
    PRINTER_STATE_INITIALIZING,
    /* Measuring idle current */
    PRINTER_STATE_MEASURING_CURRENT_IDLE,
    /* Measuring feed current */
    PRINTER_STATE_MEASURING_CURRENT_FEED,
    /* Operating */
    PRINTER_STATE_OPERATING,
} printer_state;

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

/**
 * Initialize the printer timer.
 *
 * @param tim       The timer to use. Must be reset. Will be configured for
 *                  operation. The printer_tim_handler() function should be
 *                  arranged to be called for the specified timer's
 *                  interrupts.
 * @param ck_int    Frequency of the clock fed to the timer (CK_INT).
 */
static void
printer_tim_init(volatile struct tim *tim, uint32_t ck_int)
{
    assert(printer_tim == NULL);
    printer_tim = tim;
    /* Setup counting in 1/10th of milliseconds */
    printer_tim->psc = ck_int / 10000;
    /* Select downcounting, enable auto-reload preload */
    printer_tim->cr1 = (printer_tim->cr1 & ~TIM_CR1_DIR_MASK) |
                       (TIM_CR1_DIR_VAL_DOWN << TIM_CR1_DIR_LSB) |
                       TIM_CR1_ARPE_MASK;
    /* Enable Capture/Compare 1 interrupt */
    printer_tim->dier |= TIM_DIER_CC1IE_MASK;
    /* Mark non-running */
    printer_tim_running = false;
}

/**
 * Schedule a timer for specified amount of time.
 *
 * @param ms_div_10 The time to schedule the timer for, tenths of millisecond.
 */
static void
printer_tim_schedule(uint16_t ms_div_10)
{
    assert(printer_tim != NULL);

    /* Set time to count */
    printer_tim->arr = ms_div_10;
    /* Generate an update event to transfer data to shadow registers */
    printer_tim->egr |= TIM_EGR_UG_MASK;
    /* Mark timer as running */
    printer_tim_running = true;
    /* Start counting */
    printer_tim->cr1 |= TIM_CR1_CEN_MASK | TIM_CR1_OPM_MASK;
}

void
printer_tim_handler(void)
{
    assert(printer_tim != NULL);
    assert(printer_tim_running = true);

    if (printer_tim->sr & TIM_SR_CC1IF_MASK) {
        /* If we're operating */
        if (printer_state == PRINTER_STATE_OPERATING) {
            /* Free up the printer */
            printer_set_busy(false);
        }
    }

    /* Mark timer as not running */
    printer_tim_running = false;
    /* Clear the interrupt flags */
    printer_tim->sr = 0;
}

/**
 * Sleep for specified time using printer timer.
 *
 * @param ms_div_10 The time to sleep for, tenths of millisecond.
 */
static void
printer_tim_sleep(uint16_t ms_div_10)
{
    assert(printer_tim != NULL);
    assert(printer_tim_running == false);
    printer_tim_schedule(ms_div_10);
    while (printer_tim_running) {
        asm("wfi");
    }
}

/**
 * Initialize the printer ADC
 *
 * @param adc       The ADC to use.
 * @param adc_chan  The number of the ADC channel to use.
 */
static void
printer_adc_init(volatile struct adc *adc, unsigned int adc_chan)
{
    assert(printer_adc == NULL);
    printer_adc = adc;
    printer_adc_chan = adc_chan;
}

void
printer_adc_handler(void)
{
    unsigned int sr;

    assert(printer_adc != NULL);

    /* Get the status register */
    sr = printer_adc->sr;

    /* If analog watchdog flag is set */
    if (sr & ADC_SR_AWD_MASK) {
        /* If we're operating */
        if (printer_state == PRINTER_STATE_OPERATING) {
            /* Set the busy flag */
            printer_set_busy(true);
            /* Prime the timer to clear busy flag */
            printer_tim_schedule(printer_busy_time_ms_div_10);
        }
        /* Clear the analog watchdog flag */
        printer_adc->sr &= ~ADC_SR_AWD_MASK;
    /* Else, if conversion is done */
    } else if (sr & ADC_SR_EOC_MASK) {
        /* Read the converted current (this clears the EOC flag) */
        unsigned int current = 
            (printer_adc->dr & ADC_DR_DATA_MASK) >> ADC_DR_DATA_LSB;
        /* If we're measuring the idle current */
        if (printer_state == PRINTER_STATE_MEASURING_CURRENT_IDLE) {
            if (current > printer_adc_current_idle) {
                printer_adc_current_idle = current;
            }
        /* Else, if we're measuring the feed current */
        } else if (printer_state == PRINTER_STATE_MEASURING_CURRENT_FEED) {
            if (current > printer_adc_current_feed) {
                printer_adc_current_feed = current;
            }
        }
    }
}

/**
 * Start continuous ADC conversion.
 */
static void
printer_adc_continuous_start(void)
{
    assert(printer_adc != NULL);
    assert(!(printer_adc->cr2 & ADC_CR2_ADON_MASK));
    /* Power up the ADC by setting the ADON bit */
    printer_adc->cr2 |= ADC_CR2_ADON_MASK;
    /* Wait for ADC to stabilize */
    {
        volatile unsigned int i;
        /* At least 1us at 72MHz, considering 2 cycles per loop */
        for (i = 0; i < 36; i++);
    }
    /* Set channel sampling time */
    adc_channel_set_sample_time(printer_adc, printer_adc_chan,
                                ADC_SMPRX_SMPX_VAL_28_5C);
    /* Set sequence length to one (minus one) */
    printer_adc->sqr1 = (printer_adc->sqr1 & ~ADC_SQR1_L_MASK);
    /* Set first conversion to our channel */
    printer_adc->sqr3 = (printer_adc->sqr3 & ~ADC_SQR3_SQ1_MASK) |
                        (printer_adc_chan << ADC_SQR3_SQ1_LSB);
    /* Enable EOC interrupt */
    printer_adc->cr1 |= ADC_CR1_EOCIE_MASK;
    /* Enable continuous conversion */
    printer_adc->cr2 |= ADC_CR2_CONT_MASK;
    /* Start conversion */
    printer_adc->cr2 |= ADC_CR2_ADON_MASK;
}

/**
 * Stop continuous ADC conversion.
 */
static void
printer_adc_continuous_stop(void)
{
    assert(printer_adc != NULL);
    assert(printer_adc->cr2 & ADC_CR2_ADON_MASK);
    assert(printer_adc->cr2 & ADC_CR2_CONT_MASK);
    /* Clear CONT bit and power down the ADC */
    printer_adc->cr2 &= ~(ADC_CR2_CONT_MASK | ADC_CR2_ADON_MASK);
    /* Disable EOC interrupt */
    printer_adc->cr1 &= ~ADC_CR1_EOCIE_MASK;
}

/**
 * Start ADC watchdog.
 *
 * @param low   Low watchdog threshold.
 * @param high  High watchdog threshold.
 */
static void
printer_adc_watchdog_start(unsigned int low, unsigned int high)
{
    assert(printer_adc != NULL);
    assert(!(printer_adc->cr1 & ADC_CR1_AWDEN_MASK));
    /* Power up the ADC by setting the ADON bit */
    printer_adc->cr2 |= ADC_CR2_ADON_MASK;
    /* Wait for ADC to stabilize */
    {
        volatile unsigned int i;
        /* At least 1us at 72MHz, considering 2 cycles per loop */
        for (i = 0; i < 36; i++);
    }
    /* Set watchdog channel */
    printer_adc->cr1 = (printer_adc->cr1 & ~ADC_CR1_AWDCH_MASK) |
                       (printer_adc_chan << ADC_CR1_AWDCH_LSB);
    /* Set watchdog thresholds */
    printer_adc->ltr = low;
    printer_adc->htr = high;
    /* Enable watchdog interrupt */
    printer_adc->cr1 |= ADC_CR1_AWDIE_MASK;
    /* Select single channel */
    printer_adc->cr1 |= ADC_CR1_AWDSGL_MASK;
    /* Enable watchdog on regular channels */
    printer_adc->cr1 |= ADC_CR1_AWDEN_MASK;
    /* Enable continuous conversion */
    printer_adc->cr2 |= ADC_CR2_CONT_MASK;
    /* Enable conversion */
    printer_adc->cr2 |= ADC_CR2_ADON_MASK;
}

/**
 * Transmit data to the printer, putting it into busy state.
 *
 * @param ptr               Pointer to the data to transmit.
 * @param len               Length of the data to transmit, bytes.
 */
static void
printer_transmit(const void *ptr, size_t len)
{
    assert(printer_usart != NULL);

    /* Wait for the printer to free up */
    while (printer_is_busy()) {
        asm("wfi");
    }
    /* Send the data */
    usart_transmit(printer_usart, ptr, len);
    /* The analog watchdog and the timer will free it up */
}

void
printer_init(volatile struct usart *usart,
             volatile struct adc *adc,
             unsigned int adc_chan,
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
    static const uint8_t feed_cmd[] = {0x1B, 0x4A, 0x03};

    assert(printer_usart == NULL);

    /*
     * Initialize the variables
     */
    printer_usart = usart;
    printer_busy_gpio = busy_gpio;
    printer_busy_pin = busy_pin;

    /*
     * Initialize the peripherals
     */
    printer_tim_init(tim, ck_int);
    printer_adc_init(adc, adc_chan);

    /*
     * Initialize the printer after a power-on
     */
    printer_state = PRINTER_STATE_INITIALIZING;
    /* Wait for power-up to complete */
    printer_tim_sleep(30000);
    /* Send init command */
    usart_transmit(printer_usart, init_cmd, sizeof(init_cmd));
    printer_tim_sleep(5000);
    /* Send configuration command */
    usart_transmit(printer_usart, config_cmd, sizeof(config_cmd));
    printer_tim_sleep(28);

    /*
     * Measure idle/feed current
     */
    printer_state = PRINTER_STATE_MEASURING_CURRENT_IDLE;
    printer_adc_continuous_start();
    printer_tim_sleep(5000);
    printer_state = PRINTER_STATE_MEASURING_CURRENT_FEED;
    usart_transmit(printer_usart, feed_cmd, sizeof(feed_cmd));
    printer_tim_sleep(5000);
    printer_adc_continuous_stop();

    /*
     * Enable the printer
     */
    printer_state = PRINTER_STATE_OPERATING;
    printer_adc_watchdog_start(0,
                               (printer_adc_current_idle +
                                printer_adc_current_feed) / 2);
    printer_set_busy(false);
}

void
printer_print_line(const uint8_t *line)
{
    static const uint8_t image_cmd[] = {0x12, 0x2A, 0x01, 0x30};
    printer_transmit(image_cmd, sizeof(image_cmd));
    printer_transmit(line, 0x30);
    printer_set_busy(true);
}
