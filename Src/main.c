#include <stdint.h>
#include <string.h>

#define RCC_AHB1ENR          (*(volatile uint32_t *)0x40023830u)
#define RCC_AHB1ENR_GPIOAEN  (1u << 0)
#define RCC_AHB1ENR_GPIOBEN  (1u << 1)
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

#define GPIOB_MODER          (*(volatile uint32_t *)0x40020400u)
#define GPIOB_MODER8_POS     16u
#define GPIOB_MODER8_MASK    (3u << GPIOB_MODER8_POS)
#define GPIOB_MODER9_POS     18u
#define GPIOB_MODER9_MASK    (3u << GPIOB_MODER9_POS)
#define GPIOB_OTYPER         (*(volatile uint32_t *)0x40020404u)
#define GPIOB_OTYPER_OT8     (1u << 8)
#define GPIOB_OTYPER_OT9     (1u << 9)
#define GPIOB_ODR            (*(volatile uint32_t *)0x40020414u)
#define GPIOB_ODR_ODR8       (1u << 8) /* PB8, D15, SCL */
#define GPIOB_ODR_ODR9       (1u << 9) /* PB9, D14, SDA */

#define USART2_SR            (*(volatile uint32_t *)0x40004400u)
#define USART2_SR_TXE        (1u << 7)
#define USART2_SR_RXNE       (1u << 5)
#define USART2_SR_ORE        (1u << 3)
#define USART2_DR            (*(volatile uint32_t *)0x40004404u)
#define USART2_BRR           (*(volatile uint32_t *)0x40004408u)
#define USART2_CR1           (*(volatile uint32_t *)0x4000440Cu)
#define USART2_CR1_RE        (1u << 2)
#define USART2_CR1_TE        (1u << 3)
#define USART2_CR1_RXNEIE    (1u << 5)
#define USART2_CR1_UE        (1u << 13)

#define STK_CTRL             (*(volatile uint32_t *)0xE000E010u)
#define STK_CTRL_ENABLE      (1u << 0)
#define STK_CTRL_TICKINT     (1u << 1)
#define STK_CTRL_CLKSOURCE   (1u << 2)
#define STK_LOAD             (*(volatile uint32_t *)0xE000E014u)
#define STK_VAL              (*(volatile uint32_t *)0xE000E018u)

#define SYSCLK_HZ 16000000u /* internal oscillator (HSI) after reset */
#define SYSTICK_PERIOD_CYCLES (SYSCLK_HZ / 1000u) /* 1 ms */

#define PCLK1_HZ SYSCLK_HZ
#define USART2_BAUD 115200u
#define USART2_CYCLES_PER_BIT ((PCLK1_HZ + (USART2_BAUD / 2u)) / USART2_BAUD)

#define LINE_BUFFER_SIZE 32u
#define ASCII_DEL 127u
#define UINT32_TEXT_SIZE 11u /* "4294967295" + null terminator */

#define NVIC_ISER1           (*(volatile uint32_t *)0xE000E104u)
#define NVIC_ISER1_USART2    (1u << (38u - 32u)) /* IRQ 38, ISER1 holds IRQs 32 to 63 */

#define RX_RING_SIZE         64u /* a power of 2, so an index wraps with a mask */
#define RX_RING_MASK         (RX_RING_SIZE - 1u)

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

    USART2_BRR = USART2_CYCLES_PER_BIT;
    USART2_CR1 = USART2_CR1_RE | USART2_CR1_TE | USART2_CR1_UE | USART2_CR1_RXNEIE;

    NVIC_ISER1 = NVIC_ISER1_USART2;
}

static void usart2_write_byte(char byte)
{
    while ((USART2_SR & USART2_SR_TXE) == 0u) {
    }
    USART2_DR = (uint8_t)byte;
}

static void usart2_write_string(const char *str)
{
    while (*str != '\0') {
        usart2_write_byte(*str);
        str++;
    }
}

static void usart2_write_decimal(uint32_t value)
{
    char text[UINT32_TEXT_SIZE];
    uint32_t first_digit = UINT32_TEXT_SIZE - 1u;

    text[first_digit] = '\0';
    do {
        first_digit--;
        text[first_digit] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    usart2_write_string(&text[first_digit]);
}

static volatile char rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head; // written only by handler
static volatile uint32_t rx_tail; // written only by main
static volatile uint32_t rx_dropped_count;
static volatile uint32_t rx_overrun_count;

void USART2_IRQHandler(void)
{
    uint32_t status = USART2_SR;
    if ((status & (USART2_SR_RXNE | USART2_SR_ORE)) == 0u) {
        return; // ignore the interrupt if neither RXNE nor ORE is set
    }

    char received = (char)USART2_DR; // after the SR read, this clears ORE as well as RXNE

    if ((status & USART2_SR_ORE) != 0u) {
        rx_overrun_count++;
    }
    if ((status & USART2_SR_RXNE) == 0u) {
        return; // ORE alone, so DR holds a byte this handler already took
    }

    uint32_t next_head = (rx_head + 1u) & RX_RING_MASK;
    if (next_head == rx_tail) {
        rx_dropped_count++;
        return;
    }
    rx_ring[rx_head] = received;
    rx_head = next_head;
}

static char usart2_read_byte(void)
{
    while (rx_head == rx_tail) {
    }

    char received = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1u) & RX_RING_MASK;
    return received;
}

static void bus_check_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC_AHB1ENR; // same errata wait as GPIOA's

    GPIOB_OTYPER |= GPIOB_OTYPER_OT8 | GPIOB_OTYPER_OT9;
    GPIOB_ODR |= GPIOB_ODR_ODR8 | GPIOB_ODR_ODR9; // released before output mode, so neither line dips at startup

    GPIOB_MODER = (GPIOB_MODER & ~GPIOB_MODER8_MASK)
                | ((GPIO_MODE_OUTPUT << GPIOB_MODER8_POS) & GPIOB_MODER8_MASK);
    GPIOB_MODER = (GPIOB_MODER & ~GPIOB_MODER9_MASK)
                | ((GPIO_MODE_OUTPUT << GPIOB_MODER9_POS) & GPIOB_MODER9_MASK);
}

static void run_command(const char *line)
{
    if (strcmp(line, "led") == 0) {
        GPIOA_ODR ^= GPIOA_ODR_ODR5;
        if ((GPIOA_ODR & GPIOA_ODR_ODR5) != 0u) {
            usart2_write_string("LD2 ON\r\n");
        }
        else {
            usart2_write_string("LD2 OFF\r\n");
        }
    }
    else if (strcmp(line, "millis") == 0) {
        usart2_write_decimal(millis());
        usart2_write_string(" ms\r\n");
    }
    else if (strcmp(line, "scl") == 0) {
        GPIOB_ODR ^= GPIOB_ODR_ODR8;
        if ((GPIOB_ODR & GPIOB_ODR_ODR8) != 0u) {
            usart2_write_string("SCL released\r\n");
        }
        else {
            usart2_write_string("SCL pulled low\r\n");
        }
    }
    else if (strcmp(line, "sda") == 0) {
        GPIOB_ODR ^= GPIOB_ODR_ODR9;
        if ((GPIOB_ODR & GPIOB_ODR_ODR9) != 0u) {
            usart2_write_string("SDA released\r\n");
        }
        else {
            usart2_write_string("SDA pulled low\r\n");
        }
    }
    else if (strcmp(line, "help") == 0) {
        usart2_write_string("Commands: led, millis, scl, sda, help\r\n");
    }
    else if (line[0] != '\0') {
        usart2_write_string("Unknown command: ");
        usart2_write_string(line);
        usart2_write_string("\r\n");
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

    bus_check_init();
    usart2_init();

    usart2_write_string("Hello from USART2\r\n");

    systick_start_1ms();

    char line[LINE_BUFFER_SIZE];
    uint32_t line_length = 0u;

    usart2_write_string("> ");
    for (;;) {
        char typed = usart2_read_byte();
        if (typed == '\r') {
            usart2_write_string("\r\n");
            line[line_length] = '\0';
            run_command(line);
            line_length = 0u;
            usart2_write_string("> ");
        }
        else if ((typed == '\b') || (typed == ASCII_DEL)) {
            if (line_length > 0u) {
                line_length--;
                usart2_write_string("\b \b");
            }
        }
        else if (line_length < (LINE_BUFFER_SIZE - 1u)) {
            line[line_length] = typed;
            line_length++;
            usart2_write_byte(typed);
        }
    }
}
