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

**Correction (2026-10-21):** the "could return 0" part isn't supported. With GPIOA's clock fully off, a debugger read of GPIOA_MODER returned 0xA8000000, its reset value, not 0 (see the 2026-10-21 entries). The errata asks for the delay but doesn't say what an early access returns, and reads from my own code haven't been tested. The read-back stays.

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

One change per step. I predicted before every flash and counted blinks from phone video. The instruction counts come from GCC 13.2 and 14.2 builds of the same code, whose delay loops were identical. My toolchain is GCC 14.3.1, so only the blink counts are my measurements.

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

## 2026-10-20: Reading GPIOA_MODER back in the debugger

### Setup

Debug build (-O0), no pragma. The "STM32Cube: Launch ST-Link GDB Server" configuration stops at `main`, and the peripheral register view shows GPIOA's MODER and its fields. Step Over (F10) runs one source line. The highlighted line is the next line to run: it hasn't run yet.

Reading a field is the mirror of writing one: shift right by the field's position, then AND with its width mask. OR can't pick a field out. For PA5: `(GPIOA_MODER >> GPIOA_MODER5_POS) & 3u`.

### Prediction, worked on paper

| Step | Value |
| --- | --- |
| GPIOA_MODER after reset | 0xA8000000 |
| AND with `~(3u << 10)`, which is 0xFFFFF3FF | 0xA8000000 (bits 11:10 were already 00) |
| OR with `1u << 10`, which is 0x00000400 | 0xA8000400 |
| PA5's field: `(0xA8000400 >> 10) & 3u` | 1, output |

### Results

| Point | Predicted | Read |
| --- | --- | --- |
| Stopped at `main`, RCC line next | 0xA8000000 | 0xA8000000 |
| RCC line and read-back done, setup line next | 0xA8000000 | 0xA8000000 |
| Setup line done | 0xA8000400 | 0xA8000400 |
| PA5's field (MODER5), setup line done | 01 (output) | 0b01 |

All four matched. The setup line changes only bits 11:10, PA5's mode, from 00 (input) to 01 (output). Bits 31:26 keep their reset value, so PA13, PA14 and PA15 stay in alternate function and the debugger stays connected.

### Open question

At the first point, GPIOA's clock should still be off. Several sources say an unclocked peripheral reads as 0, but the view showed the reset value. Two explanations fit: the chip answered the read without the clock, or the read failed and the view showed a stored reset value. Not yet explained.

**Answered (2026-10-21):** the chip answered the read without the clock. At the same stop, the Memory Inspector showed GPIOAEN at 0 and read GPIOA_MODER as 0xA8000000. See the 2026-10-21 entries.

### Mistakes and confusions

1. **Shifts written one place short.** I wrote `3u << 10` as 11 followed by nine zeros, which puts the field at bits 10:9. The setup line would then give 0xA8000200: PA5 stays an input, and PA4 becomes alternate function. `x << n` puts exactly n zeros to the right of x, because bit numbers start at 0.
2. **`~` written at 11 bits.** `~` flips all 32 bits: `~(3u << 10)` is 0xFFFFF3FF. As an 11-bit pattern, the AND would clear everything above bit 10, including the debugger pins.
3. **MODER 00 read as "off".** 00 is input mode. MODER holds the mode, and ODR holds the level, which only reaches the pin once MODER says output.

### Next steps: read-back

- At the first stop, check GPIOAEN in RCC_AHB1ENR and read 0x40020000 through the Memory Inspector, to settle the open question.
- Replace the busy-wait with SysTick, so the delay doesn't depend on the build.

## 2026-10-21: Reading GPIOA with its clock off

### Setup

The 2026-10-20 entry left a question open. At the first stop, GPIOA's clock should be off, yet the peripheral view showed GPIOA_MODER = 0xA8000000. Three explanations fit:

1. GPIOA's clock was already on at that stop.
2. The chip answers a read of GPIOA even with its clock off.
3. The view didn't show a live read. It knows each register's reset value from the chip's description file, so a failed read could leave that stored value on screen.

The Memory Inspector separates them. It reads raw memory at an address and stores nothing, so whatever it shows came from the chip. It opens during a debug session from the Command Palette: "Memory: Show Memory Inspector". In its settings, I set MAUs per Group to 4, so each group shows one 32-bit register. The default of 1 shows single bytes.

- RCC_AHB1ENR (0x40023830) tests explanation 1. RCC has no clock gate of its own, so it always answers.
- GPIOA_MODER (0x40020000) then separates 2 from 3.

Debug build (-O0), same main.c as 2026-10-20. I wrote the predictions down before launching.

### Results

| Reading | Stop | Tool | Predicted | Read |
| --- | --- | --- | --- | --- |
| GPIOA_MODER | `main`, RCC line next | Peripheral view | 0xA8000000 | 0xA8000000 |
| RCC_AHB1ENR (0x40023830) | same | Memory Inspector | 0x00000000 | 0x00000000 |
| GPIOA_MODER (0x40020000) | same | Memory Inspector | 0xA8000000 | 0xA8000000 |
| GPIOA_MODER (0x40020000) | setup line next | Memory Inspector | 0xA8000000 | 0xA8000000 |

- GPIOAEN, bit 0 of RCC_AHB1ENR, was 0 at the first stop, so GPIOA's clock was off. Explanation 1 is out.
- At the same stop, the Memory Inspector read GPIOA_MODER as 0xA8000000. A live read returned the register's value with the clock off: explanation 2.
- The peripheral view showed the same value, so explanation 3 isn't needed.
- The last row is the control. By then the clock is on, so that read is live under every explanation.

On this board, a debugger read of GPIOA_MODER works with GPIOA's clock off. Not tested: reads from my own code, writes with the clock off, and other registers and peripherals.

One gap remained: a 0 from RCC_AHB1ENR can't tell a real read from a failed one. The next entry closes it.

### Mistakes and confusions

1. **A stored value in the wrong tool.** My first prediction gave "the pre-filled value from the document" as the reason for a Memory Inspector reading. Only the peripheral view might show a stored value. The Memory Inspector stores nothing.
2. **Prediction and explanation didn't match.** In my first set, I predicted 0xA8000000 for GPIOA_MODER but picked explanation 3. With GPIOAEN at 0, a live 0xA8000000 is explanation 2. I redid the set before launching.
3. **One register for the whole peripheral.** I asked whether RCC is always 0 after reset. RCC_AHB1ENR resets to 0, but each register has its own reset value: RCC_CR starts with its oscillator switched on. And not every reset touches the peripherals. A debugger can reset only the CPU core, which leaves them as they were.
4. **The chip isn't the board.** The chip is the STM32F411RE. The NUCLEO-F411RE is the board around it: the chip, LD2, the buttons, the headers, and the ST-LINK, which has its own microcontroller and talks to the chip over SWD on PA13 and PA14.

## 2026-10-21: Checking the Memory Inspector against known values

### Setup

The previous entry leaned on one reading I couldn't fully trust. RCC_AHB1ENR read 0x00000000 at the first stop, but a failed read, or a display that fills in zeros, would also show 0. A reading only counts as evidence if it could have come out differently, so this run read values that can't be 0.

It also tested a second question. Every Memory Inspector reading came back as a row of four 4-byte groups, even when I asked for 4 bytes. The first group is the register at the address I typed, and each next group starts 4 bytes further on. Were the other three groups real reads?

- RCC_CR (0x40023800): RM0383 section 6.3.1 lists its reset value as 0x0000 XX81, where XX varies by chip. A real read can't be 0.
- RCC_AHB1ENR (0x40023830), after the RCC line sets bit 0.
- GPIOA's first four registers, with Length 16: GPIOA_MODER, GPIOA_OTYPER, GPIOA_OSPEEDR, and GPIOA_PUPDR. For port A, RM0383 lists 0xA800 0000, 0x0000 0000, 0x0C00 0000, and 0x6400 0000. The speed and pull values are nonzero because of the debug pins.

Debug build (-O0), same main.c, Memory Inspector at 4 bytes per group. I wrote the predictions down before launching.

### Prediction: RCC_CR isn't its printed reset value

RM0383 lists a register's value at the instant of reset, and some bits change on their own after that. HSIRDY (bit 1 of RCC_CR) is read-only, and hardware sets it once HSI, the internal 16 MHz oscillator, runs steadily. The chip runs on HSI, so by the time it reaches `main`, HSIRDY is 1.

```text
bit          3   2   1 (HSIRDY)   0 (HSION)
at reset     0   0   0            1           = 1
at main      0   0   1            1           = 3
```

So RCC_CR should read 0x0000XX83, not ...81.

### Results

| Reading | Stop | Address, Length | Predicted | Read |
| --- | --- | --- | --- | --- |
| RCC_CR | `main`, RCC line next | 0x40023800, 4 | 0x0000XX83 | 0x00006983 |
| RCC_AHB1ENR | setup line next | 0x40023830, 4 | 0x00000001 | 0x00000001 |
| GPIOA, first four registers | setup line next | 0x40020000, 16 | a8000000 00000000 0c000000 64000000 | a8000000 00000000 0c000000 64000000 |

All three matched.

- RCC_CR: XX is HSICAL, a calibration value set at the factory. Mine is 0x69.
- RCC answers real reads at the first stop, and reads at 0x40023830 show real values. So the previous entry's 0x00000000 was a real read, and GPIOA's clock really was off.
- GPIOA's four registers read their RM0383 reset values. On the previous run, with Length 4, the same row showed zeros where GPIOA_OSPEEDR and GPIOA_PUPDR read 0x0C000000 and 0x64000000 here. So the Memory Inspector fills a row past the requested Length with zeros that weren't read. Length counts bytes, and only the groups inside it count.

Also learned: likely vs known. Something I reasoned out from the manual or from other readings is only likely. A reading that could have come out differently makes it known. Before this run, "those zeros were filler" was likely; the Length 16 read made it known.

### Mistakes and confusions

1. **The printed reset value as the live value.** My first prediction for RCC_CR was 0x0000XX81, the value as printed. A reset value is a snapshot at the instant of reset, and read-only status bits can change before any code runs.
2. **The first group isn't offset 0.** I read "the first group" as the peripheral's first register. It's the register at the address I typed. At 0x40023830, group 1 is RCC_AHB1ENR, not RCC_CR.
3. **Wrong stop, again.** For RCC_AHB1ENR at the second stop, I first gave the first stop's value. By then, the RCC line has set GPIOAEN.
4. **A match that proves nothing.** I thought a 00000000 group at GPIOA_OTYPER might show a real read, since its reset value is 0. It can't: a real read and filler both show 0.
5. **One reading vouching for another.** I said the second group of the RCC_CR row was a real read because the first group was. Each group answers for itself. That group is RCC_PLLCFGR, which resets to 0x2400 3010, and it showed 00000000, outside the 4 bytes I asked for. So it was almost certainly filler. A direct read of 0x40023804 would confirm it. **Confirmed (2026-10-22):** a direct read returned 0x24003010, so it was filler. See the 2026-10-22 entry on RCC_PLLCFGR.
6. **Address vs value.** I said RCC_CR's address changed. Its address is fixed at 0x40023800; its value changed.

### Next steps: Memory Inspector

- Read RCC_PLLCFGR (0x40023804) directly, to confirm the zero in the RCC_CR row was filler.
- Byte order: read GPIOA_MODER one byte at a time in the Memory Inspector, and see how four bytes in memory make one 32-bit value.
- Replace the busy-wait with SysTick, so the delay doesn't depend on the build.

## 2026-10-22: Byte order in memory

### Setup

A 32-bit register is four bytes, and each byte has its own address: the register's address, then +1, +2, and +3. That's why the next register starts 4 bytes further on. The bytes are numbered 0 to 3 from the bits 7:0 end: byte 0 holds bits 7:0, and byte 3 holds bits 31:24. A byte's address is the register's address plus its number.

Endianness is the rule for which byte goes at the lowest address. Little-endian puts byte 0 there; big-endian puts byte 3 there.

Two Memory Inspector settings decide what a row shows:

- MAUs per Group: at 1, each byte is its own group of 2 hex digits, lowest address on the left. At 4, four bytes make one group of 8 digits.
- Group Endianness: how a 4-byte group is turned into one number. It changes the display, never memory.

Debug build (-O0), same main.c, Length 4 throughout. RCC_CR is the calibration: its value, 0x00006983, is known from the 2026-10-21 entry, and its low bytes, 83 and 69, differ, so its row shows the order. I wrote the GPIOA_MODER predictions down before launching.

### Prediction: from a value to its bytes in memory

Two steps: split the value by bits, then place each byte by address.

```text
GPIOA_MODER after the setup line = 0xA8000400

step 1, split by bits:
bits      31:24        23:16        15:8         7:0
byte      A8           00           04           00

step 2, place by address (bits 7:0 at the lowest):
address   0x40020000   0x40020001   0x40020002   0x40020003
byte      00           04           00           A8
```

The bytes move, but the two digits inside each byte don't: A8 stays A8.

### Results

| Register | Stop | Display | Predicted | Read |
| --- | --- | --- | --- | --- |
| RCC_CR (0x40023800) | `main`, RCC line next | 1 byte per group | 83 69 00 00 | 83 69 00 00 |
| GPIOA_MODER (0x40020000) | setup line next | 1 byte per group | 00 00 00 A8 | 00 00 00 a8 |
| GPIOA_MODER | setup line done | 1 byte per group | 00 04 00 A8 | 00 04 00 a8 |
| GPIOA_MODER | setup line done | 4 bytes per group, Little Endian | a8000400 | a8000400 |
| GPIOA_MODER | setup line done | 4 bytes per group, Big Endian | 000400A8 | 000400a8 |

All five matched.

- RCC_CR's row starts with 83, the byte holding bits 7:0. So that byte sits at the lowest address: this chip stores words little-endian.
- The setup line changed one byte, at 0x40020001, from 00 to 04. That byte holds bits 15:8, which include PA5's mode field, bits 11:10.
- Group Endianness changes only how the tool turns four bytes into one number. Little Endian writes them from the highest address down, which gives the register's value. Big Endian writes them from the lowest address up, which keeps memory order.

```text
memory, lowest address first   00 04 00 a8
Little Endian                  a8000400   highest address first: the register's value
Big Endian                     000400a8   lowest address first: memory order
```

### The display didn't always follow a settings change

Changing Group Endianness didn't redraw the row by itself, twice. What I did next, and whether the row then showed the new setting:

| Next action | Row redrawn |
| --- | --- |
| Typed the address again | Yes twice, no once |
| Clicked Go | No, once |
| Clicked "Load 128 more bytes above", then Go | Yes, twice |
| Changed MAUs per Group to 1 and back to 4 | Yes, once |

Each MAUs per Group change redrew the row on its own, using the current Group Endianness. Stepping to a new stop where the bytes had changed also refreshed the row. So after changing Group Endianness, I force a redraw with one of the last two actions, then check the row's shape and value against my prediction.

The Load button also reads the 128 bytes at the addresses just before the one I typed. Some registers change when they're read, such as a UART's data register, so I only use it where nothing in that range does.

### Mistakes and confusions

1. **Number order written as the row.** I first wrote GPIOA_MODER's row as A8 00 00 00: the value split into bytes, biggest first, but not placed by address. That's step 1 of 2.
2. **A hex digit taken for a byte.** For the byte holding PA5's field, I gave bits 11:8. That's one hex digit, 4 bits. The byte is two digits, bits 15:8.
3. **No method for a byte's address.** I counted bytes from the wrong end and couldn't find a byte's address. Bytes count from the bits 7:0 end, and a byte's address is the register's address plus its number.
4. **The display setting run as storing.** I predicted the Big Endian group as A8 00 04 00, the value's own byte order. Storing goes from a value into memory. The setting goes the other way: it starts from the bytes in memory and turns them back into a number.

### Next steps: byte order

- Read RCC_PLLCFGR (0x40023804) directly, one byte per group and as one group under each setting. Its four bytes all differ, and a direct read settles whether the zero in the 2026-10-21 RCC_CR row was filler.
- Replace the busy-wait with SysTick, so the delay doesn't depend on the build.

## 2026-10-22: Reading RCC_PLLCFGR directly

### Setup

In the 2026-10-21 RCC_CR row, read with Length 4 at 0x40023800, the second group showed 00000000. That group sits at 0x40023804, which is RCC_PLLCFGR. RM0383 lists its reset value as 0x2400 3010, and main.c never writes it, so it should hold that value at every stop. The zero was either filler or a real read that disagrees with the manual. Until something read 0x40023804 directly, "filler" was only likely.

A direct read also makes a fresh byte-order test. The four bytes of 0x24003010 all differ, so each display setting shows something different.

Debug build (-O0), same main.c, Length 4. I wrote the predictions down before reading.

### Prediction

```text
RCC_PLLCFGR = 0x24003010

step 1, split by bits:
bits      31:24        23:16        15:8         7:0
byte      24           00           30           10

step 2, place by address (bits 7:0 at the lowest):
address   0x40023804   0x40023805   0x40023806   0x40023807
byte      10           30           00           24
```

Little Endian writes the four bytes from the highest address down, so it should show 24003010, the register's value. Big Endian writes them from the lowest address up, so it should show 10300024.

### Results

| Display | Predicted | Read |
| --- | --- | --- |
| 4 bytes per group, Little Endian | 24003010 | 24003010 |
| 4 bytes per group, Big Endian | 10300024 | 10300024 |
| 1 byte per group | 10 30 00 24 | 10 30 00 24 |

All three matched. After I set the gear back, Little Endian read 24003010 again.

- A real read at 0x40023804 returns 0x24003010, not 0. So the 00000000 in the 2026-10-21 row was filler, and that's now known. This read could have come out 00000000, and it didn't.
- RCC_PLLCFGR sits in memory as 10 30 00 24, with the byte holding bits 7:0 at the lowest address, like RCC_CR and GPIOA_MODER.
- Little Endian gave the register's value, and Big Endian gave memory order, as with GPIOA_MODER.

### Mistakes and confusions

1. **Likely called known.** Before the read, I called the filler claim known, because the Memory Inspector had shown filler before. That was at GPIOA's addresses, in a different row. One reading can't vouch for another.
2. **Group 2's address.** I didn't see how a read at 0x40023800 involved RCC_PLLCFGR at 0x40023804. At 4 bytes per group, group 2 starts 4 bytes after the address I typed.
3. **The display setting run as storing, again.** I first predicted Little Endian as 10300024 and Big Endian as 24003010. My Big Endian rule, least significant byte at the highest address, was right, but I applied it to the value instead of to the bytes in memory. Applied to memory, the byte at the highest address, 24, goes last: 10300024.

### Next steps: direct read

- Replace the busy-wait with SysTick, so the delay doesn't depend on the build.

## 2026-10-25: SysTick delay

### Setup

The busy-wait's timing depended on the build: about 11.6 cycles per pass at -O0 and 10 at -O2, so `CYCLES_PER_DELAY_PASS` couldn't be right for both (2026-10-18). SysTick replaces it. SysTick is a 24-bit timer that counts down, built into the Cortex-M4 core. It isn't an STM32 peripheral, so it needs no RCC clock enable. It counts clock cycles in hardware, so the build can't change its period.

PM0214, ST's Cortex-M4 programming manual, documents SysTick in section 4.5. The addresses and bit positions below match Arm's CMSIS header `core_cm4.h`.

| Register | Address | What it holds |
| --- | --- | --- |
| STK_CTRL | 0xE000E010 | The switches. ENABLE (bit 0) turns counting on, TICKINT (bit 1) runs the handler each time the count reaches 0, and CLKSOURCE (bit 2) selects the processor clock, 16 MHz after reset |
| STK_LOAD | 0xE000E014 | RELOAD, in bits 23:0: the number each countdown starts from |
| STK_VAL | 0xE000E018 | The current count. Writing any value clears it |

- The count shows RELOAD, RELOAD - 1, and so on down to 0, one number per cycle, so one period is RELOAD + 1 cycles. A 1 ms period at 16 MHz is 16,000 cycles, so RELOAD is 15,999. RELOAD has 24 bits, so its largest value is 0x00FFFFFF.
- Setup is three writes, in order: STK_LOAD; then STK_VAL = 0, so the first countdown starts from RELOAD; then STK_CTRL with all three switches on. STK_CTRL goes last because it starts everything.
- Each time the count reaches 0, the CPU pauses `main`, reads entry 15 of the vector table, runs `SysTick_Handler`, and then resumes `main` where it stopped. The startup file from ST declares a weak `SysTick_Handler` that points to `Default_Handler`, an infinite loop. A function with exactly that name replaces it.
- The handler adds 1 to `elapsed_ms`, so it counts milliseconds since setup. `static` keeps other files from changing it. `volatile` makes `main` re-read it: `main` never calls the handler, so without `volatile` the compiler sees no write and can reuse its first read. Other files read the count through `millis()`.
- `delay_ms()` saves the start time and waits while `millis() - start < ms`. Unsigned subtraction wraps modulo 2^32, like the counter, so the difference stays right when the counter rolls over, after about 49.7 days. Comparing `millis() < start + ms` would break near the wrap: `start + ms` can wrap to a small number and end the wait at once.

```c
#define STK_CTRL             (*(volatile uint32_t *)0xE000E010u)
#define STK_CTRL_ENABLE      (1u << 0)
#define STK_CTRL_TICKINT     (1u << 1)
#define STK_CTRL_CLKSOURCE   (1u << 2)
#define STK_LOAD             (*(volatile uint32_t *)0xE000E014u)
#define STK_VAL              (*(volatile uint32_t *)0xE000E018u)

#define SYSCLK_HZ               16000000u            /* internal oscillator (HSI) after reset */
#define SYSTICK_PERIOD_CYCLES   (SYSCLK_HZ / 1000u)  /* 1 ms */
#define BLINK_HALF_PERIOD_MS    500u

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

static void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms) {
    }
}
```

In `main`, `systick_start_1ms()` runs after the MODER line, and the loop calls `delay_ms(BLINK_HALF_PERIOD_MS)` where the busy-wait used to be.

Debug build (-O0) unless noted. The pragma build used `#pragma GCC optimize("O2")` as the first line of main.c, as on 2026-10-18, and it came out afterward. I wrote the predictions down before each flash.

### Results

| Build | Predicted | Result |
| --- | --- | --- |
| Debug (-O0) | 60 blinks per minute | 60 blinks per minute |
| `#pragma GCC optimize("O2")` | 60 blinks per minute | 60 blinks per minute |
| Pragma, with `volatile` deleted from `elapsed_ms` | Steady light; `elapsed_ms` unchanged between two pauses | Steady light; `elapsed_ms` read 3281, then 5156; paused, the CPU was in the `while` loop of `delay_ms` |

- The busy-wait gave 51 to 52 blinks per minute at -O0 and about 60 at -O2 (2026-10-18). SysTick gave 60 in both builds.
- In the third run, the handler kept counting: 1,875 more ticks between the two pauses. `main` never saw them. Without `volatile`, the compiler can assume the counter doesn't change during the wait, so `millis() - start` stays 0 and the wait never ends. LD2 stayed on because the first XOR ran before the first wait. `volatile` went back in afterward.

### Mistakes and confusions

1. **RELOAD kept fixed.** For a 1 ms period, I first kept STK_LOAD at 3 and worked out how many 4-cycle periods fit in 1 ms. RELOAD is the setting I choose so that one period lasts the whole 16,000 cycles.
2. **Off by one.** Then I put 16,000 in STK_LOAD. The count runs from 16,000 down to 0, which is 16,001 numbers, so every period would run one cycle long. The rule is RELOAD = cycles - 1.
3. **volatile explained by its definition.** I said `volatile` "guarantees each read/write occurs." That's what it does, not why `elapsed_ms` needs it. The reason is that the handler changes it and `main` never calls the handler.
4. **Keywords.** I wrote `volatile static`, but C calls a storage-class keyword anywhere but first obsolescent, so it's `static volatile`. I also put `static` on `millis()`, which would have made the read function private to main.c.
5. **The skipped-write model.** For a flag set in a handler, I said the write "got skipped." The handler is kept, and its write lands in memory; what the optimizer drops is `main`'s re-reads. My prediction for the third run still assumed the counter wouldn't change, and the board showed it climbing.

### Next steps: SysTick

- Drive LD2 through GPIOA_BSRR (offset 0x18), which sets or resets a pin in one write, so an interrupt can't land inside a read-modify-write.