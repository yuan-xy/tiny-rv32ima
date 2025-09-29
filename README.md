# tiny-rv32ima

tiny-rv32ima is a platform-agnostic library that adapts [CNLohr's mini-rv32ima](https://github.com/cnlohr/mini-rv32ima) RISC-V emulator core for embedded systems. It provides a lightweight environment for running Linux on resource-constrained devices.

## Overview

This library implements a RISC-V emulator capable of running Linux without MMU support. It makes use of external SPI PSRAM for system memory and of an SD card for storage. All hardware abstraction must be provided by the user, as this library does not include any platform-specific HAL implementation.

## Features

- 32-bit RISC-V emulation (RV32IMA instruction set)
- No-MMU Linux support
- Simple HAL interface for platform portability
- SD card(or Nor-Flash) support for kernel and filesystem storage
- Supports hibernating to SD card

## VM Configuration (`vm_config.h`)

The `vm_config.h` file is used to configure key parameters of the emulator at compile time. 

Typical options include:

- **File names for images:**  
  - `KERNEL_FILENAME`, `BLK_FILENAME`, `DTB_FILENAME`, `SNAPSHOT_FILENAME`  
    Set the filenames for the Linux kernel, root filesystem, device tree blob, and VM snapshot.

- **Device tree size:**  
  - `DTB_SIZE`  
    Size (in bytes) of the device tree binary. The DTB built by the matching buildroot toolchain is 2048 bytes.

- **RAM size:**  
  - `EMULATOR_RAM_MB`  
    Amount of emulated RAM (in megabytes).

- **Kernel command line:**  
  - `KERNEL_CMDLINE`  
    Command line passed to the Linux kernel.

- **Timing and update options:**  
  - `EMULATOR_TIME_DIV`, `EMULATOR_FIXED_UPDATE`  
    `EMULATOR_TIME_DIV` sets by how much real time is divided when exposed to the emulator. On slow MCUs, this should preferably be set to a power of 2 to prevent scheduling interrupt overloading during boot. `EMULATOR_FIXED_UPDATE` controls whether the microsecond clock is tied to instruction count.

- **Cache configuration:**  
  - `CACHE_LINE_SIZE`, `CACHE_SET_SIZE`, `OFFSET_BITS`, `INDEX_BITS`  
    Configure the size of the emulator’s internal cache.

**Example:**
```c
#define KERNEL_FILENAME "IMAGE"
#define BLK_FILENAME "ROOTFS"
#define DTB_FILENAME "DTB"
#define EMULATOR_RAM_MB 8
#define KERNEL_CMDLINE "console=hvc0 root=fe00"

// Cache configuration
#define CACHE_LINE_SIZE 16
#define OFFSET_BITS 4 // log2(CACHE_LINE_SIZE)
#define CACHE_SET_SIZE 4096
#define INDEX_BITS 12 // log2(CACHE_SET_SIZE)
```

## Hardware abstraction layer

All HAL interfaces required by tiny-rv32ima can be implemented as either C functions or preprocessor defines/macros, depending on platform and performance needs. **All HAL functions/defines must be implemented; they can be provided as stubs if not needed.**

### Console HAL (`hal_console.h`)
```c
void console_putc(char c);        // Output a character
void console_puts(char* s);       // Output a string
bool console_available(void);     // Check if input is available
char console_read(void);          // Read a character from input
bool pwr_button(void);            // Check power button state
```

### Memory HAL (`hal_psram.h`)
```c
void psram_select(void);          // Select the memory chip
void psram_deselect(void);        // Deselect the memory chip
void psram_spi_write(uint8_t* buf, size_t sz);  // Write data to memory
void psram_spi_read(uint8_t* buf, size_t sz);   // Read data from memory
```

### SD Card / Flash HAL (`hal_sd.h`)
```c
void sd_select(void);             // Select the SD card (CS low)
void sd_deselect(void);           // Deselect the SD card (CS high)
uint8_t sd_spi_byte(uint8_t b);   // Transfer a byte over SPI and return the received byte
void sd_led_on(void);             // (Optional) Turn on SD activity LED
void sd_led_off(void);            // (Optional) Turn off SD activity LED
```

### Timing HAL (`hal_timing.h`)
```c
void timing_delay_ms(uint32_t ms);     // Delay for ms milliseconds
void timing_delay_us(uint32_t us);     // Delay for us microseconds
uint64_t timing_micros(void);          // Get current time in microseconds since boot
```
### Custom CSR HAL (`hal_csr.h`)
```c
void custom_csr_write(uint16_t csrno, uint32_t value); // Write to a custom CSR
uint32_t custom_csr_read(uint16_t csrno);              // Read from a custom CSR, must return 0 for undefined CSR
```
- These are used for custom device control, such as SPI or other hardware features.

## VM Interface

The library provides the following core functions:

```c
// Initialize the virtual machine hardware
void vm_init_hw(void);

// Start the virtual machine
// prev_power_state: previous power state for resume functionality
// Returns: emulator status code
int start_vm(int prev_power_state);

// Get current power state
uint8_t vm_get_powerstate(void);

// Save power state
// state: power state to save
// Returns: saved state
uint8_t vm_save_powerstate(uint8_t state);
```

### Emulator Status Codes
```c
enum emulatorCode {
    EMU_POWEROFF,    // System power off request
    EMU_HIBERNATE,   // System hibernating
    EMU_REBOOT,      // System reboot request
    EMU_GET_SD,      // Get previous power state from SD card
    EMU_RUNNING,     // VM is running
    EMU_UNKNOWN      // Unknown state
};
```

## How to use the library

To use tiny-rv32ima in a project:

1. **Initialize hardware and peripherals** as needed (SPI, GPIO, console, etc.).
2. **Call `vm_init_hw()`** once to initialize the PSRAM and SD card.
3. **Start the emulator loop** by repeatedly calling `start_vm()` with the previous VM state. This function runs the RISC-V virtual machine and returns a status code (see `enum emulatorCode`). This function is typically called in a loop, to handle power state changes or reboots.

**Example usage (from pico-rv32ima):**
```c
void core1_entry() {
    sleep_ms(250);
    vm_init_hw();

    int vm_state = EMU_GET_SD;
    while (true) {
        vm_state = start_vm(vm_state);
    }
}
```
- `vm_init_hw()` initializes the PSRAM and SD card.
- `start_vm()` runs the emulator and returns the next power state to handle (e.g., reboot, power off, etc.).

## Linux images
The Linux distribution meant to be used with tiny-rv32ima is built from [buildroot-tiny-rv32ima](https://github.com/tvlad1234/buildroot-tiny-rv32ima.git). Pre-built images are available in the Releases section of the buildroot-tiny-rv32ima repo.

## Implementation examples

## License

This library is based on [CNLohr's mini-rv32ima](https://github.com/cnlohr/mini-rv32ima), which is licensed under the MIT License. See the original project's license for terms and conditions.

The `pff` directory includes the full PetitFatFs library by ChaN (http://elm-chan.org/fsw/ff/00index_p.html), with only minor modifications (such as `mmcbbp.c`). PetitFatFs is subject to its own license (see source files for details).
