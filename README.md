# Game Boy Emulator for Tuya T5 Pocket

A minimal Game Boy emulator application for the Tuya T5 Pocket device, based on the SDL2-GNUBoy engine.

## Features

- Game Boy and Game Boy Color emulation
- LVGL display integration (RGB565 to monochrome conversion handled by LVGL)
- ADC joystick and GPIO button input
- Tuya audio system integration
- SD card storage support

## Building

This app follows the standard Tuya app build process. Ensure you have:

1. TuyaOS SDK properly configured
2. GNUBoy core integrated in `gnuboy/` directory (copied from SDL2-GNUBoy)
3. Board support package (BSP) for TUYA_T5AI_POCKET

Build using the standard Tuya build system.

## Configuration

Configure ROM and save paths via Kconfig:

- `CONFIG_GB_EMU_ROM_PATH`: Default ROM path (default: "/sd/roms/gb")
- `CONFIG_GB_EMU_SAVE_PATH`: Default save path (default: "/sd/saves/gb")

## Status

This is a **minimal implementation** that provides:

- ✅ Basic emulator initialization
- ✅ Display adapter (LVGL)
- ✅ Input adapter (joystick + buttons)
- ✅ Audio adapter (Tuya audio)
- ✅ Storage adapter
- ⚠️ ROM loading (needs implementation)
- ⚠️ ROM browser UI (needs implementation)
- ⚠️ Save state management (needs implementation)

## Next Steps

To complete the implementation:

1. Implement ROM file browser using LVGL
2. Add ROM loading from SD card
3. Implement save state save/load
4. Add menu system for emulator options
5. Optimize performance for embedded system

## License

Based on GNUBoy (GPLv2) and SDL2-GNUBoy (GPLv2).
