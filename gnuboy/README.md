# GNUBoy Core - Integrated Copy

This directory contains an integrated copy of the SDL2-GNUBoy emulator core.

## Source

Based on SDL2-GNUBoy: https://github.com/AlexOberhofer/SDL2-GNUBoy

## License

GNU GPLv2 (see LICENSE.md in original repository)

## Structure

- `src/` - Core emulator source files
- `include/` - Header files
- `lib/` - Compression libraries (gzip, xz)
- `sys/` - System-specific implementations (using nix/ for embedded)

## Integration

This copy has been integrated into the Tuya T5 Pocket Game Boy emulator project.
The system-specific implementations (video, audio, input) are provided by the
main application in `../src/` rather than using the SDL2 implementations.
