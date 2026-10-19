# Learning log

Each entry records what I set out to do, what I predicted, what happened, and the mistakes I made.

## 2026-10-16: Toolchain and const placement

### Experiment setup

Installed STM32CubeIDE for Visual Studio Code and created an empty CMake project for the STM32F411RE. It has ST's startup file and linker script, and no HAL.

### Experiment: what does `const` change for a table?

```c
static const uint8_t lookup_table[256] = { 1, 2, 3 };

volatile uint32_t table_index = 7;
volatile uint32_t table_value;

int main(void)
{
    table_value = lookup_table[table_index];

    for (;;) {
    }
}
```

The index is `volatile`, so the table is read at run time. The compiler or linker can drop a table nothing reads, and then both builds would show identical numbers.

Prediction:

| Build | Section | Flash | RAM |
| --- | --- | --- | --- |
| with `const` | `.rodata` | 256 B | 0 |
| without `const` | `.data` | 256 B | 256 B |

Without `const`, the initial values still need flash, because RAM is empty at power-on. Startup code copies them into RAM before `main` runs.

Result, from the linker's memory report:

| Build | RAM | FLASH |
| --- | --- | --- |
| with `const` | 1984 B | 1180 B |
| without `const` | 2240 B | 1180 B |
| difference | +256 B | 0 |

The result matches the prediction. Flash did not change because the 256 bytes only moved, from `.rodata` to the initial-value image of `.data`, and both live in flash.

`static` gives the table internal linkage: no other file can link to it. It does not change where the table lives or how big it is.

## 2026-10-16: Which pin drives LD2

Method: search the manuals for exact labels instead of reading them. The board manual (UM1724) says what is wired to what. The chip manual (RM0383) says what each register bit does and where each peripheral sits in memory.

- UM1724 Rev 17, section 7.6 "LEDs" (p. 24): LD2 connects to Arduino signal D13, which is PA5 or PB13 depending on the board.
- Table 16, "ARDUINO connectors on NUCLEO-F401RE and NUCLEO-F411RE" (p. 44): on this board, D13 is PA5.
- PA5 means port A, pin 5, so the LED is driven through the GPIOA peripheral.

## 2026-10-17: GPIOA clock and PA5 mode

### Step 1: turn on GPIOA's clock

Every peripheral starts with its clock off. Writes to an unclocked peripheral are ignored, with no error. So the first write turns on GPIOA's clock in RCC.

- RCC base: 0x4002 3800 (RM0383 Table 1, p. 38)
- RCC_AHB1ENR: offset 0x30; bit 0 is GPIOAEN (section 6.3.9, p. 118)
- Address: 0x40023800 + 0x30 = 0x40023830

```c
#define RCC_AHB1ENR          (*(volatile uint32_t *)0x40023830u)
#define RCC_AHB1ENR_GPIOAEN  (1u << 0)

RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
(void)RCC_AHB1ENR;
```

`|=` is already a read-modify-write. OR with 1 forces bit 0 on. OR with 0 leaves the other 31 bits unchanged.

The second line reads the register back. After the enable write, the clock can take 2 AHB cycles to start (errata item "Delay after an RCC peripheral clock enabling": ES0299 section 2.2.7 for the sibling STM32F401; ES0287 is the F411's own errata sheet). If the next read of GPIOA_MODER landed too early, it could return 0, and the write-back would turn PA13 and PA14 into inputs and cut off the debugger. ST's HAL does the same read-back.

### Step 2: make PA5 an output

- GPIOA base: 0x4002 0000 (Table 1). MODER offset: 0x00 (Table 26, GPIO register map).
- Address: 0x40020000 + 0x00 = 0x40020000
- Each pin y has a 2-bit field at bits 2y+1:2y. For PA5, that is bits 11:10.
- Field values: 00 input (reset state), 01 general-purpose output, 10 alternate function, 11 analog.
- Port A's MODER resets to 0xA800 0000: PA13, PA14, and PA15 start in alternate function mode, and PA13 and PA14 carry the debugger. A plain `=` would cut the debugger off, so only PA5's two bits change: clear them, then OR in the new value.

```c
#define GPIOA_MODER        (*(volatile uint32_t *)0x40020000u)
#define GPIOA_MODER5_POS   10u
#define GPIOA_MODER5_MASK  (3u << GPIOA_MODER5_POS)
#define GPIO_MODE_OUTPUT   1u

GPIOA_MODER = (GPIOA_MODER & ~GPIOA_MODER5_MASK)
            | ((GPIO_MODE_OUTPUT << GPIOA_MODER5_POS) & GPIOA_MODER5_MASK);
```

AND with the inverted mask: 0 clears bits 11:10, and 1 keeps every other bit. OR with the shifted value: 1 sets a bit, and 0 keeps it.

### Mistakes and how they were caught

1. **Wrong bit number.** I said GPIOAEN was bit 1. It is bit 0. Bit numbers start at 0 on the right, and the "0:" and "1:" lines under a field are values, not positions. Bit 1 is GPIOBEN, so the LED would have stayed dark with no error.
2. **Wrong name.** My first draft named the RCC register `GPIOA`. Registers now use the manual's names, so anyone can search the manual and check the address.
3. **A number, not a register.** `#define GPIOA_MODER 0x40020000u` makes the name a plain number, so assigning to it does not compile: GCC reports "lvalue required as left operand of assignment." A register define has to dereference a volatile pointer.
4. **Macro without parentheses.** With `#define MASK 3u << 10`, `~MASK` expands to `(~3u) << 10`, which is 0xFFFFF000. That clears the fields for PA0 through PA5, not just PA5. At reset those fields are already 00, so the bug stays hidden until a pin from PA0 to PA4 is configured before this line runs.
5. **Masked before shifting.** `(value & MASK) << 10` gives 0, because the mask sits at bit 10 and the value at bit 0. PA5 would have stayed an input. Shifting the value into position first, then masking, fixes it.

### Next

- Find GPIOA_ODR's offset and write the statement that toggles PA5.
- Add a delay loop.
- Flash the board, and read MODER back in the debugger to confirm PA5's mode.

## 2026-10-17: Toggle, delay, and the first blink

### Step 3: toggle PA5

- GPIOA_ODR: offset 0x14 (Table 26). Address: 0x40020000 + 0x14 = 0x40020014. It resets to 0, so every pin starts low.
- Bit y sets pin y's output level: 1 is high, 0 is low. PA5 is bit 5, so the mask is 1u << 5 = 0x20.

```c
#define GPIOA_ODR       (*(volatile uint32_t *)0x40020014u)
#define GPIOA_ODR_ODR5  (1u << 5)

GPIOA_ODR ^= GPIOA_ODR_ODR5;
```

XOR gives 1 only where the two bits differ. So XOR with 1 flips a bit, and XOR with 0 keeps it. The mask has a 1 only at bit 5, so each pass flips PA5 and leaves the other 31 bits alone.

```text
operator   mask bit 0   mask bit 1
OR  |      keep         force 1      set
AND &      force 0      keep         clear
XOR ^      keep         flip         toggle
```

### Step 4: a busy-wait delay

Prediction without a delay: one pass of the loop takes on the order of 10 cycles, so at 16 MHz PA5 flips about 1.6 million times per second. The eye can't follow on and off faster than roughly 50 to 100 times per second, so LD2 would look steadily lit and dimmer, not blinking.

The delay wastes time by counting. For 0.5 s on and 0.5 s off:

- cycles = rate × time = 16,000,000 × 0.5 = 8,000,000 cycles
- passes = cycles ÷ cycles per pass = 8,000,000 ÷ 10 = 800,000 passes

```c
#define SYSCLK_HZ               16000000u   /* internal oscillator (HSI) after reset */
#define CYCLES_PER_DELAY_PASS   10u         /* estimate; measure on hardware */
#define HALF_SECOND_PASSES      (SYSCLK_HZ / 2u / CYCLES_PER_DELAY_PASS)

for (;;) {
    GPIOA_ODR ^= GPIOA_ODR_ODR5;

    for (volatile uint32_t pass = 0; pass < HALF_SECOND_PASSES; pass++) {
    }
}
```

The counter is volatile so that every pass has to happen. Without it, the loop has no observable effect, and an optimized build may shrink it or delete it. Debug builds (-O0) don't optimize, so they hide this bug.

### Result

Debug build, flashed through the on-board ST-LINK: LD2 blinks. A phone stopwatch counted 10 blinks in 10 seconds, so the 10-cycles-per-pass estimate held, within what counting by eye can resolve.

**Correction (2026-10-18):** a 60-second video of the same build counted 51 to 52 blinks, so a pass takes about 11.6 cycles at -O0, not 10. A 10-second count can't resolve that difference. See the 2026-10-18 entry.

### Mistakes and confusions

1. **Counting 1s instead of positions.** "Bit 5" is the sixth position from the right, counting from 0, whatever value it holds. My count landed right only because PA5 happened to be high.
2. **Cycles vs passes.** I said half a second was 400,000 cycles and 400,000 passes. It's 8,000,000 cycles, and passes = cycles ÷ cycles per pass, so the two numbers can't match.
3. **Two meanings of "cycle".** A clock cycle is one tick at 16 MHz. One on plus one off is a blink, or a period. The 8,000,000 clock cycles of one wait are all "on" or all "off", not split between them.

Also learned: a comma can't go inside a C number. `3,200,000u` is three numbers, and in a loop condition it compiles silently and the loop runs zero times.

### Next steps: first blink

- Remove volatile in an optimized build and watch the blink speed up or vanish.
- Read GPIOA_MODER back in the debugger to confirm PA5's mode.

## 2026-10-18: What -O2 does to the delay loop

### Setup

The optimization level is how hard the compiler works at making the translated code faster. -O0, used by the Debug build, turns every line of C into its own instructions, in order. -O2 may shorten, merge, reorder, or skip work, as long as the program's result stays the same.

The compiler only has to keep the program's result, which the C standard calls observable behavior. Every read and write of a `volatile` object counts as part of the result. How long the code takes does not, and a busy-wait loop's only product is time.

For this demo only, the first line of main.c was:

```c
#pragma GCC optimize("O2")
```

It tells GCC to compile every function below it in main.c at -O2, so the Debug build ran optimized code without touching CMake. It came out afterward.

One change per step. I predicted before every flash and counted blinks from phone video. The instruction counts come from a GCC 13.2 build of the same code; my toolchain is GCC 14.3.1, so only the blink counts are my measurements.

### Results

Steps 1 to 4 ran with the pragma.

| Step | Delay | Counter | Predicted | Result |
| --- | --- | --- | --- | --- |
| Baseline, -O0 | inline | `volatile` | | 51 to 52 blinks per minute, about 11.6 cycles per pass |
| 1 | inline | `volatile` | slightly faster: 7 instructions per pass become 6 | about 60 blinks per minute, 10 cycles per pass |
| 2 | inline | not `volatile` | faster, about 150 per minute | 34 blinks in 10 s (about 204 per minute), about 3 cycles per pass |
| 3 | in its own function | not `volatile` | same rate as step 2 | no visible blink, a steady light |
| 4 | in its own function | `volatile` | about 60 per minute | 20 blinks in 20 s |

From blinks to cycles per pass: one blink is two waits, one wait is 800,000 passes, and the clock runs 16,000,000 cycles per second. At 51.5 blinks per minute, a wait takes 60 ÷ 103 ≈ 0.58 s, which is about 9.3 million cycles, or 11.6 per pass.

What each step showed:

- **Step 1.** `volatile` forces 2 reads and 1 write of `pass` on every pass: one read to compare, then a read and a write for the increment. All 800,000 passes stay. -O2 trims only what isn't protected: at -O0 every pass reloads the limit from flash, and at -O2 the limit stays in a register. 60 ÷ 51.5 ≈ 1.17, and 7 ÷ 6 ≈ 1.17.
- **Step 2.** Without `volatile`, none of those accesses has to happen. The compiler kept the loop but replaced `pass` with a register counting down: 2 instructions per pass and no memory access. The "half second" became about 0.15 s.
- **Step 3.** The same loop moved into `static void delay_half_second(void)`, and the compiler deleted it. `main` now toggles LD2 millions of times per second, which looks like a steady light. Stepping through `main` in the debugger alternates between the `for (;;)` line and the XOR line and never stops on the `delay_half_second()` line, because that line has no instructions left.
- **Step 4.** `volatile` back inside the function brings the blink back at the step 1 rate.

### Takeaways

- A busy-wait counter must be `volatile`. Without it, the compiler may shrink the delay or delete it. Here a harmless-looking refactor flipped it from shrunk to deleted. A Release build or a compiler upgrade can do the same.
- The C rules say the compiler *may* delete the loop. Whether it does is the compiler's choice: step 2 kept a deletable loop, and step 3 didn't.
- Even with `volatile`, busy-wait timing depends on the build: about 11.6 cycles per pass at -O0 and 10 at -O2, so `CYCLES_PER_DELAY_PASS` can't be right for both. A hardware timer (SysTick) fixes that.

Also learned: in optimized code, the debugger's highlighted line jumps around. Instructions from different lines get reordered and merged, and a line whose work was removed never gets highlighted.

### Mistakes and confusions

1. **Same result, same speed.** For step 1, I first predicted the same rate, because the result stays the same. Time isn't part of the result, and making code faster is -O2's job.
2. **Step 3 predicted from step 2.** I assumed the compiler would repeat step 2's countdown. It isn't bound to: C allowed deleting the loop both times.
3. **Units in the chain.** I wrote that 800,000 passes take 60 seconds. That's one half-second wait. Writing the chain one row at a time fixed it: cycles per wait, then seconds per wait, then two waits per blink.
4. **Half an answer, again.** "Too fast for the eye" isn't the whole picture. LD2 looks like a steady light, dimmer than fully on, because it's on only about half the time.

### Next steps: optimization

- Read GPIOA_MODER back in the debugger to confirm PA5's mode.
- Replace the busy-wait with SysTick, so the delay doesn't depend on the build.