# Changes from SDL2-GNUBoy Codebase

This document outlines the key differences and modifications made to port SDL2-GNUBoy to the Tuya T5 Pocket AI platform.

## Overview

The port adapts SDL2-GNUBoy (a desktop Game Boy emulator) to run on the embedded Tuya T5 Pocket AI platform, which uses ESP-IDF, LVGL for display, and Tuya's hardware abstraction layer.

## Major Changes

### 1. Platform and Build System

**Original (SDL2-GNUBoy):**
- Desktop Linux/Windows/macOS target
- Makefile-based build system
- SDL2 for graphics, audio, and input
- Standard C library

**Port (Tuya T5 Pocket):**
- Embedded ESP-IDF platform (ARM Cortex-M33)
- CMake-based build system integrated with TuyaOS
- LVGL for display rendering
- Tuya hardware abstraction layer (TAL) for system services

### 2. Display System

**Original:**
- SDL2 window, renderer, and texture
- Direct framebuffer rendering
- RGB565 or indexed color modes
- Hardware-accelerated scaling

**Port:**
- LVGL canvas widget for display
- RGB565 framebuffer passed to LVGL
- LVGL handles RGB565 → monochrome conversion internally
- Thread-safe display updates using `lv_vendor_disp_lock/unlock`
- Display adapter in `src/gb_display.c` implementing `sys.h` video interface

**Key Files:**
- `src/gb_display.c` - Display adapter implementation
- `gnuboy/src/lcd.c` - Modified to work with LVGL canvas

### 3. Audio System

**Original:**
- SDL2 audio callback system
- Automatic audio buffering and playback
- Configurable sample rate and channels

**Port:**
- Tuya audio codec API (`tdd_audio`)
- PCM submission via `pcm_submit()` function
- Audio adapter in `src/gb_audio.c`
- Audio initialization handled by Tuya hardware registration

**Key Files:**
- `src/gb_audio.c` - Audio adapter implementation
- `gnuboy/sys/nix/nix_tuya.c` - System timer for audio pacing

### 4. Input System

**Original:**
- SDL2 keyboard and joystick events
- Event queue system
- Configurable key bindings via RC system

**Port:**
- Tuya ADC joystick API (`tdd_joystick`)
- Tuya GPIO button API (`tdd_button_gpio`)
- Direct mapping to gnuboy's `keystates` array
- Input adapter in `src/gb_input.c` (to be implemented)

**Key Files:**
- `src/gb_input.c` - Input adapter (placeholder)
- Input polling integrated into main emulation loop

### 5. Storage System

**Original:**
- Standard POSIX file I/O
- RC file system for configuration
- Save state management

**Port:**
- Tuya file system API (`tkl_fs`)
- SD card support for ROMs and saves
- Storage adapter in `src/gb_storage.c`
- System path initialization simplified

**Key Files:**
- `src/gb_storage.c` - Storage adapter with system function implementations

### 6. Memory Management

**Original:**
- Standard heap allocation
- Static arrays in BSS/data sections
- No memory constraints

**Port:**
- **PSRAM_HEAP allocation** for large arrays (256 KB `patpix` array)
- Dynamic allocation using `tal_psram_malloc()` instead of static allocation
- Removed color palette arrays (160 KB saved: `palmap[32768]` and `crsmap[4][32768]`)
- Memory-optimized build flags (`-Os`, `-ffunction-sections`, `-fdata-sections`)

**Key Changes:**
- `gnuboy/src/lcd.c`: `patpix` array allocated dynamically in `lcd_reset()`
- `gnuboy/src/palette.c`: Removed color mapping arrays (DMG-only emulation)

### 7. Removed Features

**RC (Run Command) System:**
- Removed `rccmds.c`, `rckeys.c`, `rcvars.c`, `rcfile.c`, `exports.c`, `split.c`, `path.c`
- Configuration now handled via Tuya Kconfig system
- No runtime configuration file support

**Debug System:**
- Removed `debug.c` (691 lines)
- Stub implementations for `debug_trace` and `debug_disassemble()` in `nix_tuya.c`
- Debug features disabled to save memory

**Color Game Boy (CGB) Support:**
- Removed color palette mapping arrays (`palmap`, `crsmap`)
- Simplified `pal_getcolor()` function
- Only DMG (monochrome) emulation supported
- CGB-specific code paths remain but are never executed

**SDL2-Specific System Files:**
- Removed `sys/nix/nix.c` (SDL2 timer implementation)
- Removed `sys/nix/io_pipe.c` and `sys/nix/io_network.c` (link cable emulation)
- Created `sys/nix/nix_tuya.c` with Tuya-specific implementations

### 8. System Interface Adaptations

**Original (`sys/nix/nix.c`):**
```c
void *sys_timer() {
    Uint32 *tv = malloc(sizeof *tv);
    *tv = SDL_GetTicks() * 1000;
    return tv;
}
```

**Port (`sys/nix/nix_tuya.c`):**
```c
void *sys_timer(void) {
    int *tv = (int *)tal_malloc(sizeof(int));
    if (tv) {
        *tv = (int)tal_system_get_millisecond();
    }
    return tv;
}
```

**Other System Functions:**
- `sys_elapsed()`: Uses `tal_system_get_millisecond()` instead of SDL2 ticks
- `sys_sleep()`: Uses `tal_system_sleep()` instead of `SDL_Delay()`
- `doevents()`: Stub implementation (events handled separately)
- `die()`: Logs error via Tuya logging system instead of `exit()`

### 9. Main Entry Point

**Original:**
- `src/main.c` with SDL2 initialization
- Command-line argument parsing
- RC file loading
- Event loop with SDL2 event polling

**Port:**
- `src/tuya_main.c` with TuyaOS entry point (`tuya_app_main()`)
- Thread-based application model
- Hardware registration via `board_register_hardware()`
- Emulator initialization and main loop in `user_main()`

### 10. Build Configuration

**Original:**
- Simple Makefile with compiler flags
- SDL2-config for library detection
- Direct compilation to executable

**Port:**
- CMakeLists.txt integrated with TuyaOS build system
- Source file filtering (removed unused files)
- Compiler optimizations for size (`-Os`)
- Link-time optimization flags
- Library linking with TuyaOS components

**Key Build Changes:**
```cmake
# Removed files to save space
list(REMOVE_ITEM GNUBOY_SRCS
    ${APP_PATH}/gnuboy/src/debug.c
    ${APP_PATH}/gnuboy/src/main.c
    ${APP_PATH}/gnuboy/src/rccmds.c
    # ... other RC system files
)

# Compiler optimizations
target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        "-Os"  # Optimize for size
        "-ffunction-sections"
        "-fdata-sections"
        "-DNDEBUG"
)
```

## File Structure Changes

### New Files
- `src/gb_display.c` - Display adapter
- `src/gb_audio.c` - Audio adapter
- `src/gb_input.c` - Input adapter (placeholder)
- `src/gb_storage.c` - Storage adapter with system functions
- `src/gb_emu_main.c` - Main emulator logic
- `src/tuya_main.c` - TuyaOS entry point
- `gnuboy/sys/nix/nix_tuya.c` - Tuya system implementations
- `include/gb_*.h` - Adapter headers

### Modified Files
- `gnuboy/src/lcd.c` - PSRAM allocation, LVGL integration
- `gnuboy/src/palette.c` - Removed color arrays, simplified for DMG
- `gnuboy/src/emu.c` - Works with new system interface
- `CMakeLists.txt` - Build configuration

### Removed Files
- `gnuboy/src/debug.c`
- `gnuboy/src/main.c` (SDL2 main)
- `gnuboy/src/rccmds.c`, `rckeys.c`, `rcvars.c`, `rcfile.c`, `exports.c`, `split.c`, `path.c`
- `gnuboy/sys/nix/nix.c` (replaced by `nix_tuya.c`)
- `gnuboy/sys/nix/io_pipe.c`, `io_network.c`

## API Mappings

| SDL2 Function | Tuya Equivalent | Location |
|--------------|----------------|----------|
| `SDL_GetTicks()` | `tal_system_get_millisecond()` | `nix_tuya.c` |
| `SDL_Delay()` | `tal_system_sleep()` | `nix_tuya.c` |
| `SDL_CreateWindow()` | LVGL canvas | `gb_display.c` |
| `SDL_RenderPresent()` | `lv_obj_invalidate()` | `gb_display.c` |
| `SDL_PollEvent()` | `tdd_joystick_read()`, `tdd_gpio_button_get_value()` | `gb_input.c` |
| `SDL_OpenAudio()` | `tdd_audio` initialization | `gb_audio.c` |
| `SDL_QueueAudio()` | `tdd_audio_write()` | `gb_audio.c` |
| `fopen()`, `fread()` | `tkl_fs_open()`, `tkl_fs_read()` | `gb_storage.c` |

## Memory Optimizations

1. **Dynamic PSRAM Allocation**: 256 KB `patpix` array moved to PSRAM_HEAP
2. **Removed Color Arrays**: 160 KB saved by removing `palmap` and `crsmap`
3. **Removed Debug Code**: ~50 KB saved by removing `debug.c`
4. **Removed RC System**: ~30 KB saved by removing configuration system
5. **Compiler Optimizations**: Size optimization flags reduce binary size

**Total Memory Savings**: ~500 KB of RAM freed by moving to PSRAM and removing unused features

## Limitations

1. **DMG Only**: No Color Game Boy (CGB) emulation support
2. **No Debug Features**: Debugger and disassembler removed
3. **No Runtime Configuration**: No RC file support, configuration via Kconfig only
4. **No Link Cable**: No multiplayer/link cable emulation
5. **Fixed Display**: No scaling options (160x144 → 168x384 monochrome display)
6. **Limited Audio**: Audio codec configuration fixed at initialization

## Future Enhancements

- [ ] Implement ROM file browser using LVGL
- [ ] Add save state support
- [ ] Implement input adapter fully
- [ ] Add emulator menu system
- [ ] Optimize audio buffering and pacing
- [ ] Add screenshot functionality

## Summary

The port successfully adapts SDL2-GNUBoy to the embedded Tuya platform by:
- Replacing SDL2 with Tuya hardware APIs and LVGL
- Using dynamic PSRAM allocation for large arrays
- Removing desktop-specific features (RC system, debug tools)
- Simplifying for DMG-only emulation
- Integrating with TuyaOS build and runtime systems

The result is a memory-efficient Game Boy emulator optimized for the Tuya T5 Pocket AI platform.
