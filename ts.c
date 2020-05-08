/*
 * Thermal Spectrum - a ZX-printer interface for a thermal printer module
 */
#include "printer.h"
#include "zxprinter.h"
#include <init.h>
#include <usart.h>
#include <gpio.h>
#include <afio.h>
#include <rcc.h>
#include <nvic.h>
#include <exti.h>
#include <misc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void tim2_irq_handler(void) __attribute__ ((isr));
void
tim2_irq_handler(void)
{
    printer_handler();
}

void tim3_irq_handler(void) __attribute__ ((isr));
void
tim3_irq_handler(void)
{
    zxprinter_tim_handler();
}

void
exti_handler(void)
{
    zxprinter_write_handler();
    /* Clear the interrupt */
    EXTI->pr |= (1 << ZXPRINTER_PIN_WRITE);
}

#define EXTI_IRQ_HANDLER(_name) \
    void                                    \
    _name(void)                             \
    {                                       \
        exti_handler();                     \
    }                                       \
    void _name(void) __attribute__ ((isr))

EXTI_IRQ_HANDLER(exti0_irq_handler);
EXTI_IRQ_HANDLER(exti1_irq_handler);
EXTI_IRQ_HANDLER(exti2_irq_handler);
EXTI_IRQ_HANDLER(exti3_irq_handler);
EXTI_IRQ_HANDLER(exti4_irq_handler);
EXTI_IRQ_HANDLER(exti9_5_irq_handler);
EXTI_IRQ_HANDLER(exti15_10_irq_handler);

int
main(void)
{
    /* Output line buffer */
    volatile uint8_t line_buf[48] = {0,};
    /* Basic init */
    init();

    /*
     * Enable clocks
     */
    /* Enable APB2 clock to I/O ports A and B, and AFIO */
    RCC->apb2enr |= RCC_APB2ENR_IOPAEN_MASK | RCC_APB2ENR_IOPBEN_MASK |
                    RCC_APB2ENR_IOPCEN_MASK | RCC_APB2ENR_AFIOEN_MASK;

    /*
     * Setup printer with USART2 at 9600 baud rate and
     * TIM2 timer fed by doubled 36MHz APB1 clock
     */
    /* Configure status LED */
    gpio_pin_conf(GPIO_C, 13,
                  GPIO_MODE_OUTPUT_2MHZ, GPIO_CNF_OUTPUT_GP_OPEN_DRAIN);

    /* Configure printer TX pin (PA2) */
    gpio_pin_conf(GPIO_A, 2,
                  GPIO_MODE_OUTPUT_50MHZ,
                  GPIO_CNF_OUTPUT_AF_PUSH_PULL);
    /* Configure printer RX pin (PA3) */
    gpio_pin_conf(GPIO_A, 3,
                  GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOATING);
    /* Enable clock to USART2 */
    RCC->apb1enr |= RCC_APB1ENR_USART2EN_MASK;
    /* Initialize the USART with 9600 baud rate, based on 36MHz PCLK1 */
    usart_init(USART2, 36 * 1000 * 1000, 9600);

    /* Enable clock to the timer */
    RCC->apb1enr |= RCC_APB1ENR_TIM2EN_MASK;
    /* Enable timer interrupt */
    nvic_int_set_enable(NVIC_INT_TIM2);

    /* Initialize printer module */
    printer_init(USART2, TIM2, 72000000, GPIO_C, 13);

    /*
     * Setup ZX Printer interface with GPIO_B for I/O and
     * the motor-timing TIM3 fed by doubled 36MHz APB1 clock
     */
    /* Enable clock to the timer */
    RCC->apb1enr |= RCC_APB1ENR_TIM3EN_MASK;
    /* Initialize ZX Printer interface module */
    zxprinter_init(GPIO_B, TIM3, 72000000, line_buf);
    /* Enable timer interrupt */
    nvic_int_set_enable(NVIC_INT_TIM3);
    /* Enable interrupt on the rising edge of the WRITE pin */
    afio_exti_set_port(ZXPRINTER_PIN_WRITE, AFIO_EXTI_PORT_B);
    EXTI->imr |= 1 << ZXPRINTER_PIN_WRITE;
    EXTI->rtsr |= 1 << ZXPRINTER_PIN_WRITE;
    nvic_int_set_enable_ext(ZXPRINTER_PIN_WRITE);

    /* Transmit */
    do {
        asm ("wfi");
        if (zxprinter_line_count_in > zxprinter_line_count_out) {
            printer_print_line((const uint8_t *)line_buf);
            zxprinter_line_count_out++;
        }
    } while (1);
}
