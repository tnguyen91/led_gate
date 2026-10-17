# STM32F411 bare-metal learning log

Bare-metal C on an STM32 Nucleo-F411RE board. No HAL and no generated setup code: each peripheral is configured by writing its registers directly, with addresses and bit positions taken from ST's manuals.

This is a learning record, not a finished project. [LOG.md](LOG.md) records each step: what I predicted, what happened, and the mistakes I made.

## Hardware

- NUCLEO-F411RE: STM32F411RE microcontroller (Arm Cortex-M4), with an on-board ST-LINK for flashing and debugging

## Toolchain

- STM32CubeIDE for Visual Studio Code: CMake build with arm-none-eabi-gcc (GNU Tools for STM32 14.3.1+st.2)
- Empty project: ST's startup file and linker script, no HAL

## Status

| Step | State |
|---|---|
| Const placement experiment | Done, measured in the linker's memory report |
| Turn on GPIOA's clock | Written, not yet checked on hardware |
| Set PA5 to output | Written, not yet checked on hardware |
| Toggle LD2 (PA5) | Not started |
| Delay loop | Not started |

## References

- RM0383: STM32F411xC/E reference manual (registers and memory map)
- UM1724: STM32 Nucleo-64 boards user manual (board wiring)

Both are free PDFs from st.com.