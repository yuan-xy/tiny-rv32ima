# tiny-rv32ima - Code Buddy Guide

This is a platform-agnostic RISC-V emulator library that adapts CNLohr's mini-rv32ima emulator for embedded systems. It provides a lightweight environment for running Linux on resource-constrained devices.

## Project Overview

- **Architecture**: RISC-V RV32IMA emulator for embedded systems
- **Target**: No-MMU Linux support on resource-constrained devices
- **Dependencies**: External SPI PSRAM for memory, SD card/Nor-Flash for storage
- **License**: MIT (based on CNLohr's mini-rv32ima), with PetitFatFs under its own license

## Core Components

### Main Library Entry Points
- `tiny-rv32ima.h` - Main header including emulator
- `emulator/emulator.h` - Core emulator interface and state codes
- `emulator/emulator.c` - Main emulator implementation with VM lifecycle

### Hardware Abstraction Layers
All HAL interfaces must be implemented by the user:
- `hal_console.h` - Console I/O (putc, puts, read, available, pwr_button)
- `hal_psram.h` - External PSRAM access (select, deselect, read/write)
- `hal_sd.h` - SD card/Flash interface (select, deselect, SPI transfer)
- `hal_timing.h` - Timing functions (delay_ms, delay_us, micros)
- `hal_csr.h` - Custom CSR handling for device control

### Memory Subsystem
- `cache/cache.h/.c` - Cache layer between emulator and PSRAM
- `psram/psram.h/.c` - PSRAM controller interface
- **Cache Configuration**: Set via `vm_config.h` (CACHE_LINE_SIZE, CACHE_SET_SIZE, OFFSET_BITS, INDEX_BITS)

### File System
- `pff/` - PetitFatFs library for SD card access
- Modified with `mmcbbp.c` for block device support

## Key Configuration (`vm_config.h`)

Must be provided by user project:
```c
// File names
#define KERNEL_FILENAME "IMAGE"
#define BLK_FILENAME "ROOTFS"  
#define DTB_FILENAME "DTB"
#define SNAPSHOT_FILENAME "SNAPSHOT"

// System configuration
#define EMULATOR_RAM_MB 8
#define DTB_SIZE 2048
#define KERNEL_CMDLINE "console=hvc0 root=fe00"

// Performance tuning
#define EMULATOR_TIME_DIV 1
#define EMULATOR_FIXED_UPDATE 0

// Cache configuration
#define CACHE_LINE_SIZE 16
#define CACHE_SET_SIZE 4096
#define OFFSET_BITS 4
#define INDEX_BITS 12
```

## VM Lifecycle API

### Core Functions
- `void vm_init_hw(void)` - Initialize PSRAM and SD card
- `int start_vm(int prev_power_state)` - Main emulator loop
- `uint8_t vm_get_powerstate(void)` - Get saved power state from SD
- `uint8_t vm_save_powerstate(uint8_t state)` - Save power state to SD

### Power States (`enum emulatorCode`)
- `EMU_RUNNING` - VM is active
- `EMU_REBOOT` - Request reboot
- `EMU_POWEROFF` - Request power off
- `EMU_HIBERNATE` - Hibernate to SD card
- `EMU_GET_SD` - Get state from SD
- `EMU_UNKNOWN` - Unknown state

## Typical Usage Pattern

```c
void vm_thread() {
    vm_init_hw();
    
    int vm_state = EMU_GET_SD;
    while (true) {
        vm_state = start_vm(vm_state);
    }
}
```

## CSR/MMIO Mapping

### Custom CSRs (Linux HVC calls)
- `0x139` - Console output (putc)
- `0x140` - Console input (read)
- `0x150-0x155` - Block device operations
- `0x170` - Hibernate request

### MMIO Devices
- `0x10000000` - 8250/16550 UART emulation
- Custom devices via `hal_csr.h`

## Build Integration

This is a library, not a standalone application. Include in your project:
1. Copy library files to your project
2. Implement all HAL interfaces
3. Create `vm_config.h` with your configuration
4. Link against the library

## Linux Image Requirements

Use pre-built images from [buildroot-tiny-rv32ima](https://github.com/tvlad1234/buildroot-tiny-rv32ima):
- Kernel image with no-MMU support
- Root filesystem image
- Device tree blob (DTB)
- DTB must contain cmdline placeholder starting with "abcd"

## Memory Architecture

- **Emulated RAM**: Configured via `EMULATOR_RAM_MB`
- **Physical Storage**: External SPI PSRAM
- **Cache**: Configurable set-associative cache between emulator and PSRAM
- **Block Device**: SD card accessed via PetitFatFs for kernel/filesystem storage