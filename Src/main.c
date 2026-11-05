#include <stdint.h>

#define RCC_AHB1ENR          (*(volatile uint32_t *)0x40023830u)
#define RCC_AHB1ENR_GPIOAEN  (1u << 0)
#define RCC_APB1ENR          (*(volatile uint32_t *)0x40023840u)
#define RCC_APB1ENR_USART2EN (1u << 17)

#define GPIOA_MODER          (*(volatile uint32_t *)0x40020000u)
#define GPIOA_MODER5_POS     10u
#define GPIOA_MODER5_MASK    (3u << GPIOA_MODER5_POS)
#define GPIOA_MODER2_POS     4u
#define GPIOA_MODER2_MASK    (3u << GPIOA_MODER2_POS)
#define GPIOA_MODER3_POS     6u
#define GPIOA_MODER3_MASK    (3u << GPIOA_MODER3_POS)

#define GPIO_MODE_OUTPUT     1u
#define GPIO_MODE_ALTERNATE  2u

#define GPIOA_ODR            (*(volatile uint32_t *)0x40020014u)
#define GPIOA_ODR_ODR5       (1u << 5)

#define GPIOA_AFRL           (*(volatile uint32_t *)0x40020020u)
#define GPIOA_AFRL2_POS      8u
#define GPIOA_AFRL2_MASK     (0xFu << GPIOA_AFRL2_POS)
#define GPIOA_AFRL3_POS      12u
#define GPIOA_AFRL3_MASK     (0xFu << GPIOA_AFRL3_POS)
#define GPIO_AF_USART2       7u

#define STK_CTRL             (*(volatile uint32_t *)0xE000E010u)
#define STK_CTRL_ENABLE      (1u << 0)
#define STK_CTRL_TICKINT     (1u << 1)
#define STK_CTRL_CLKSOURCE   (1u << 2)
#define STK_LOAD             (*(volatile uint32_t *)0xE000E014u)
#define STK_VAL              (*(volatile uint32_t *)0xE000E018u)

#define SYSCLK_HZ 16000000u /* internal oscillator (HSI) after reset */
#define SYSTICK_PERIOD_CYCLES (SYSCLK_HZ / 1000u) /* 1 ms */
#define BLINK_HALF_PERIOD_MS 500u

static volatile uint32_t elapsed_ms;

void SysTick_Handler(void)
{
    elapsed_ms++;
}

uint32_t millis(void) 
{ 
    return elapsed_ms;
}

static void systick_start_1ms(void)
{
    STK_LOAD = SYSTICK_PERIOD_CYCLES - 1u;
    STK_VAL = 0u;
    STK_CTRL = STK_CTRL_ENABLE | STK_CTRL_TICKINT | STK_CTRL_CLKSOURCE;
}

static void usart2_init(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC_APB1ENR; // same errata wait as GPIOA's

    GPIOA_AFRL = (GPIOA_AFRL & ~GPIOA_AFRL2_MASK)
                | ((GPIO_AF_USART2 << GPIOA_AFRL2_POS) & GPIOA_AFRL2_MASK);
    GPIOA_AFRL = (GPIOA_AFRL & ~GPIOA_AFRL3_MASK)
                | ((GPIO_AF_USART2 << GPIOA_AFRL3_POS) & GPIOA_AFRL3_MASK);

    GPIOA_MODER = (GPIOA_MODER & ~GPIOA_MODER2_MASK)
                | ((GPIO_MODE_ALTERNATE << GPIOA_MODER2_POS) & GPIOA_MODER2_MASK);
    GPIOA_MODER = (GPIOA_MODER & ~GPIOA_MODER3_MASK)
                | ((GPIO_MODE_ALTERNATE << GPIOA_MODER3_POS) & GPIOA_MODER3_MASK);
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms) {
    }
}

int main(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    /* Errata: the clock can take 2 AHB cycles to start. Reading the enable
    * register back waits that out before GPIOA is touched. */
    (void)RCC_AHB1ENR;

    GPIOA_MODER = (GPIOA_MODER & ~GPIOA_MODER5_MASK)
                | ((GPIO_MODE_OUTPUT << GPIOA_MODER5_POS) & GPIOA_MODER5_MASK);

    usart2_init();

    systick_start_1ms();

    for (;;) {
        GPIOA_ODR ^= GPIOA_ODR_ODR5;
        delay_ms(BLINK_HALF_PERIOD_MS);
    }
}
