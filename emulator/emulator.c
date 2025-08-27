#include <stddef.h>

#include "emulator.h"
#include "../psram/psram.h"
#include "../cache/cache.h"
#include "../pff/pff.h"

#include "hal_console.h"
#include "hal_csr.h"
#include "hal_timing.h"

#include "vm_config.h"

int time_divisor = EMULATOR_TIME_DIV;
int fixed_update = EMULATOR_FIXED_UPDATE;
int do_sleep = 1;
int single_step = 0;
int fail_on_all_faults = 0;
int hibernate_request = 0;

uint32_t ram_amt = EMULATOR_RAM_MB * 1024 * 1024;
const char kernel_cmdline[] = KERNEL_CMDLINE;

FATFS fatfs;
uint32_t blk_buf[512 / 4];

unsigned long blk_size = 67108864;
unsigned long blk_transfer_size;
unsigned long blk_offs;
unsigned long blk_ram_ptr;
FRESULT blk_err;

static inline uint32_t HandleException(uint32_t ir, uint32_t retval);
static uint32_t HandleControlStore(uint32_t addy, uint32_t val);
static uint32_t HandleControlLoad(uint32_t addy);
static void HandleOtherCSRWrite(uint16_t csrno, uint32_t value);
static uint32_t HandleOtherCSRRead(uint16_t csrno);

#define MINIRV32WARN(x...) // consoleprintf(x);
#define MINIRV32_DECORATE static
#define MINI_RV32_RAM_SIZE ram_amt
#define MINIRV32_IMPLEMENTATION
#define MINIRV32_POSTEXEC(pc, ir, retval)             \
    {                                                 \
        if (retval > 0)                               \
        {                                             \
            if (fail_on_all_faults)                   \
            {                                         \
                return 3;                             \
            }                                         \
            else                                      \
                retval = HandleException(ir, retval); \
        }                                             \
    }
#define MINIRV32_HANDLE_MEM_STORE_CONTROL(addy, val) \
    if (HandleControlStore(addy, val))               \
        return val;
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(addy, rval) rval = HandleControlLoad(addy);
#define MINIRV32_OTHERCSR_WRITE(csrno, value) HandleOtherCSRWrite(csrno, value);
#define MINIRV32_OTHERCSR_READ(csrno, rval) \
    {                                       \
        rval = HandleOtherCSRRead(csrno);   \
    }

#define MINIRV32_CUSTOM_MEMORY_BUS

#define MINIRV32_STORE4(ofs, val) cache_write(ofs, &val, 4)
#define MINIRV32_STORE2(ofs, val) cache_write(ofs, &val, 2)
#define MINIRV32_STORE1(ofs, val) cache_write(ofs, &val, 1)

static inline uint32_t MINIRV32_LOAD4(uint32_t ofs)
{
    uint32_t val;
    cache_read(ofs, &val, 4);
    return val;
}

static inline uint16_t MINIRV32_LOAD2(uint32_t ofs)
{
    uint16_t val;
    cache_read(ofs, &val, 2);
    return val;
}

static inline uint8_t MINIRV32_LOAD1(uint32_t ofs)
{
    uint8_t val;
    cache_read(ofs, &val, 1);
    return val;
}

static inline int8_t MINIRV32_LOAD1_SIGNED(uint32_t ofs)
{
    int8_t val;
    cache_read(ofs, &val, 1);
    return val;
}

static inline int16_t MINIRV32_LOAD2_SIGNED(uint32_t ofs)
{
    int16_t val;
    cache_read(ofs, &val, 2);
    return val;
}

#include "mini-rv32ima.h"

struct MiniRV32IMAState core;

const char spinner[] = "/-\\|";

uint8_t vm_get_powerstate(void)
{
    FRESULT rc = pf_open("STAT");
    if (rc)
        return EMU_UNKNOWN;

    uint8_t state;
    UINT br;

    rc = pf_read(&state, 1, &br);

    if (rc || !br)
        return EMU_UNKNOWN;
    return state;
}

uint8_t vm_save_powerstate(uint8_t state)
{
    FRESULT rc = pf_open("STAT");
    if (rc)
        return rc;

    pf_lseek(0);
    UINT bw;
    rc = pf_write(&state, 1, &bw);
    if (rc)
        return rc;
    rc = pf_write(0, 0, &bw);
    return rc;
}

void vm_init_hw(void)
{
    if (psram_init())
        console_puts("PSRAM OK\n\r");
    else
        console_panic("PSRAM ERR\n\r");

    FRESULT rc;
    int tries = 0;

    do
    {
        rc = pf_mount(&fatfs);
        if (rc)
            tries++;
        timing_delay_ms(200);
    } while (rc && tries < 5);

    if (rc)
        console_panic("\rError initalizing SD\n\r");

    console_puts("\rSD init OK\n\r");
}

void psram_load_file(uint32_t addr)
{
    UINT br;
    uint32_t total_bytes = 0;
    uint8_t cnt = 0;

    int chunks = ram_amt / sizeof(blk_buf);

    for (int i = 0; i < chunks; i++)
    {
        FRESULT rc = pf_read(blk_buf, sizeof(blk_buf), &br); /* Read a chunk of file */
        if (rc)
            console_panic("Error loading image\n\r");
        if (!br)
            break; /* Error or end of file */

        psram_access(addr, br, true, blk_buf);
        total_bytes += br;
        addr += br;

        if (total_bytes % (16 * 1024) == 0)
        {
            cnt++;
            console_putc(spinner[cnt % 4]);
            console_putc('\r');
        }
    }
}

int start_vm(int prev_power_state)
{
    while (!pwr_button() && prev_power_state != EMU_REBOOT)
        ;

    cache_reset();

    if (prev_power_state == EMU_GET_SD)
        prev_power_state = vm_get_powerstate();

    if (prev_power_state == EMU_RUNNING)
        console_puts("System hasn't been cleanly shutdown\n\r");

    FRESULT rc;
    hibernate_request = 0;
    int LOAD_SNAPSHOT = 0;

    if (prev_power_state == EMU_HIBERNATE)
        LOAD_SNAPSHOT = 1;

    if (LOAD_SNAPSHOT)
    {
        console_puts("Restoring hibernation file\n\r");
        rc = pf_open(SNAPSHOT_FILENAME);
    }
    else
    {
        console_puts("Loading kernel image\n\r");
        rc = pf_open(KERNEL_FILENAME);
    }

    if (rc)
        console_panic("Error opening image file\n\r");

    psram_load_file(0);

    if (LOAD_SNAPSHOT)
    {
        UINT br;
        rc = pf_read(&core, sizeof(struct MiniRV32IMAState), &br); /* Read a chunk of file */
        if (br != sizeof(struct MiniRV32IMAState))
            console_panic("Not enough bytes for core!\n\r");
    }

    if (rc)
        console_panic("Error loading image\n\r");

    if (!LOAD_SNAPSHOT)
    {
        uint32_t dtb_ptr = ram_amt - DTB_SIZE;
        rc = pf_open(DTB_FILENAME);
        if (rc)
            console_panic("Error opening DTB file\n\r");
        psram_load_file(dtb_ptr);

        // DTB RAM size patching, specified ammount in DTB must be 0x3ffc000
        for (int i = dtb_ptr; i < ram_amt; i += 4)
        {
            uint32_t dtbram = MINIRV32_LOAD4(i); // dtb[0x13c];

            if (dtbram == 0x00c0ff03)
            {
                uint32_t validram = dtb_ptr;
                dtbram = (validram >> 24) | (((validram >> 16) & 0xff) << 8) | (((validram >> 8) & 0xff) << 16) | ((validram & 0xff) << 24);
                MINIRV32_STORE4(i, dtbram);
                break;
            }
        }

        // DTB cmdline patching, cmdline placeholder in DTB must start with "abcd"
        // and MUST be long enough to fit the actual cmdline
        const char *c = kernel_cmdline;
        uint32_t ptr = dtb_ptr;
        while (MINIRV32_LOAD4(++ptr) != 0x64636261 && ptr < ram_amt)
            ;
        do
            MINIRV32_STORE1(ptr++, *(c++));
        while (*(c - 1));

        core.regs[10] = 0x00;                                                // hart ID
        core.regs[11] = dtb_ptr ? (dtb_ptr + MINIRV32_RAM_IMAGE_OFFSET) : 0; // dtb_pa (Must be valid pointer) (Should be pointer to dtb)

        core.extraflags |= 3; // Machine-mode.
        core.pc = MINIRV32_RAM_IMAGE_OFFSET;
    }
    vm_save_powerstate(EMU_RUNNING);

    rc = pf_open(BLK_FILENAME);
    if (rc)
        console_panic("Error opening block device image\n\r");

    console_puts("Starting RISC-V VM\n\n\r");

    long long instct = -1;

    uint64_t rt;
    uint64_t lastTime = (fixed_update) ? 0 : (timing_micros() / time_divisor);

    int instrs_per_flip = single_step ? 1 : 4096;
    for (rt = 0; rt < instct + 1 || instct < 0; rt += instrs_per_flip)
    {
        uint64_t *this_ccount = ((uint64_t *)&core.cyclel);
        uint32_t elapsedUs = 0;
        if (fixed_update)
            elapsedUs = *this_ccount / time_divisor - lastTime;
        else
            elapsedUs = timing_micros() / time_divisor - lastTime;
        lastTime += elapsedUs;

        int ret = MiniRV32IMAStep(&core, NULL, 0, elapsedUs, instrs_per_flip); // Execute upto 1024 cycles before breaking out.
        switch (ret)
        {
        case 0:
            break;
        case 1:
            if (do_sleep)
                timing_delay_ms(1);
            *this_ccount += instrs_per_flip;
            break;
        case 3:
            instct = 0;
            break;
        case 0x7777:
            vm_save_powerstate(EMU_REBOOT);
            return EMU_REBOOT; // syscon code for reboot
        case 0x5555:
            vm_save_powerstate(EMU_POWEROFF);
            return EMU_POWEROFF; // syscon code for power-off
        default:
            vm_save_powerstate(EMU_UNKNOWN);
            return EMU_UNKNOWN;
            break;
        }

        if (hibernate_request)
        {
            vm_save_powerstate(EMU_HIBERNATE);
            cache_flush();

            rc = pf_open(SNAPSHOT_FILENAME);
            if (rc)
                console_panic("Error opening hibernation file\n\r");

            uint32_t addr = 0;
            pf_lseek(0);

            int bw;
            uint32_t total_bytes = 0;
            uint8_t cnt = 0;
            int chunks = ram_amt / sizeof(blk_buf);

            for (int i = 0; i < chunks; i++)
            {
                psram_access(addr, sizeof(blk_buf), false, blk_buf);
                addr += sizeof(blk_buf);

                rc = pf_write(blk_buf, 512, &bw);
                if (rc)
                    console_panic("Error writing RAM image\n\r");

                total_bytes += bw;
                if (total_bytes % (16 * 1024) == 0)
                {
                    cnt++;
                    console_putc(spinner[cnt % 4]);
                    console_putc('\r');
                }
            }

            rc = pf_write(&core, sizeof(struct MiniRV32IMAState), &bw);
            if (rc)
                console_panic("Error writing core image\n\r");

            rc = pf_write(0, 0, &bw);
            if (rc)
                console_panic("Error finalizing write\n\r");

            console_puts("\n\rHibernating.\n\r");
            return EMU_HIBERNATE;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Functions for the emulator
//////////////////////////////////////////////////////////////////////////

// Exceptions handling

static inline uint32_t HandleException(uint32_t ir, uint32_t code)
{
    // Weird opcode emitted by duktape on exit.
    if (code == 3)
    {
        // Could handle other opcodes here.
    }
    return code;
}

// CSR handling (Linux HVC console and block device)
static inline void HandleOtherCSRWrite(uint16_t csrno, uint32_t value)
{
    if (csrno == 0x139)
        console_putc(value);
    else if (csrno == 0x151)
    {
        blk_ram_ptr = value - MINIRV32_RAM_IMAGE_OFFSET;
        // printf("\nblock op mem ptr %x\n", value);
    }
    else if (csrno == 0x152)
    {
        blk_offs = value;
        blk_err = pf_lseek(blk_offs);
        // printf("block op offset %x\n", value);
    }
    else if (csrno == 0x153)
    {
        blk_transfer_size = value;
        // printf("\nblock op transfer size %d\n", value);
    }
    else if (csrno == 0x154)
    {
        unsigned int nblocks = blk_transfer_size >> 9; // divide by 512
        while (nblocks--)
        {
            if (value)
            {
                //  printf("block op write\n");
                for (int i = 0; i < 512 / 4; i++)
                {
                    blk_buf[i] = MINIRV32_LOAD4(blk_ram_ptr);
                    blk_ram_ptr += 4;
                }
                int x;
                blk_err = pf_write(blk_buf, 512, &x);
            }
            else
            {
                int x;
                blk_err = pf_read(blk_buf, 512, &x);
                for (int i = 0; i < 512 / 4; i++)
                {
                    MINIRV32_STORE4(blk_ram_ptr, blk_buf[i]);
                    blk_ram_ptr += 4;
                }
                // printf("block op read\n");
            }
        }
    }
    else if (csrno == 0x170)
        hibernate_request = 1;
    else
        custom_csr_write(csrno, value);
}

static inline uint32_t HandleOtherCSRRead(uint16_t csrno)
{
    if (csrno == 0x140)
    {
        if (console_available())
        {
            return console_read();
        }
        else
            return -1;
    }
    else if (csrno == 0x150)
    {
        return blk_size;
    }
    else if (csrno == 0x155)
    {
        return blk_err;
    }
    return custom_csr_read(csrno);
}

// MMIO handling (8250 UART)

static uint32_t HandleControlStore(uint32_t addy, uint32_t val)
{
    if (addy == 0x10000000) // UART 8250 / 16550 Data Buffer
        console_putc(val);
    return 0;
}

static uint32_t HandleControlLoad(uint32_t addy)
{
    // Emulating a 8250 / 16550 UART
    if (addy == 0x10000005)
        return 0x60 | console_available();
    else if (addy == 0x10000000 && console_available())
        return console_read();

    return 0;
}
