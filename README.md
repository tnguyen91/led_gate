# STM32F411 bare-metal learning log

Bare-metal C on an STM32 Nucleo-F411RE board. No HAL and no generated setup code: each peripheral is configured by writing its registers directly, with addresses and bit positions taken from ST's manuals.

This is a learning record, not a finished project. [LOG.md](LOG.md) records each step: what I predicted, what happened, and the mistakes I made.

## Demo

https://github.com/user-attachments/assets/637563ab-eb27-46e3-a006-d548f7ea5eea

## Hardware

- NUCLEO-F411RE: STM32F411RE microcontroller (Arm Cortex-M4), with an on-board ST-LINK for flashing and debugging

## Toolchain

- STM32CubeIDE for Visual Studio Code: CMake build with arm-none-eabi-gcc (GNU Tools for STM32 14.3.1+st.2)
- Empty project: ST's startup file and linker script, no HAL
- Debugging: ST's peripheral register view, and the Memory Inspector for reading raw memory at an address

## Status

| Step | State |
| --- | --- |
| Const placement experiment | Done, measured in the linker's memory report |
| Turn on GPIOA's clock | Done, verified by the blink |
| Set PA5 to output | Done, verified by the blink |
| Toggle LD2 (PA5) | Done |
| Busy-wait delay | Done: 51 to 52 blinks per minute (Debug build, 60-second video) |
| Remove volatile in an optimized build | Done: without `volatile`, -O2 shrank the delay or deleted it (see LOG) |
| Read MODER back in the debugger | Done: 0xA8000000 before the setup line, 0xA8000400 after; PA5's field 01 (output) |
| Read GPIOA with its clock off | Done: a debugger read of GPIOA_MODER with GPIOAEN = 0 returned 0xA8000000 |
| Check the Memory Inspector against known values | Done: RCC_CR read 0x00006983 at `main`, RCC_AHB1ENR 0x00000001 after the enable, and GPIOA's first four registers their reset values |
| Byte order in memory | Not started |
| SysTick delay | Not started |

## Build and run

Open the folder in VS Code with the STM32CubeIDE for Visual Studio Code extension. Start the "STM32Cube: Launch ST-Link GDB Server" debug configuration: it builds, flashes the board, and stops at `main`. Press Continue, and LD2 blinks a little under once per second.

## References

- RM0383: STM32F411xC/E reference manual (registers and memory map)
- UM1724: STM32 Nucleo-64 boards user manual (board wiring)

Both are free PDFs from st.com.
