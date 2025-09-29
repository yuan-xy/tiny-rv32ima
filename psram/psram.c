#include "psram.h"
#include "hal_psram.h"
#include "hal_timing.h"

#define PSRAM_CMD_RES_EN 0x66
#define PSRAM_CMD_RESET 0x99
#define PSRAM_CMD_READ_ID 0x9F
#define PSRAM_CMD_READ 0x03
#define PSRAM_CMD_READ_FAST 0x0B
#define PSRAM_CMD_WRITE 0x02
#define PSRAM_KGD 0x5D

void psram_cmd(uint8_t cmd)
{
    psram_select();
    psram_spi_write(&cmd, 1);
    psram_deselect();
}

uint8_t psram_read_kgd(void)
{
    uint8_t buf[6] = {0};
    buf[0] = PSRAM_CMD_READ_ID;
    psram_select();
    psram_spi_write(buf, 4);
    psram_spi_read(buf, 6);
    for(int i=0; i<6; i++)
        printf("%x ", buf[i]);
    printf("\n");
    psram_deselect();

    if (buf[1] == PSRAM_KGD)
        return 1;
    return 0;
}

uint8_t psram_init(void)
{
    psram_cmd(PSRAM_CMD_RES_EN);
    psram_cmd(PSRAM_CMD_RESET);
    timing_delay_ms(10);
    return psram_read_kgd();
}

void psram_access(uint32_t addr, unsigned int size, bool write, void *bufP)
{
    uint8_t cmdAddr[5];
    unsigned int cmdSize = 4;

    if (write)
        cmdAddr[0] = PSRAM_CMD_WRITE;
    else
    {
        cmdAddr[0] = PSRAM_CMD_READ_FAST;
        cmdSize++;
    }

    cmdAddr[1] = addr >> 16;
    cmdAddr[2] = addr >> 8;
    cmdAddr[3] = addr;

    psram_select();
    psram_spi_write(cmdAddr, cmdSize);

    if (write)
        psram_spi_write(bufP, size);
    else
        psram_spi_read(bufP, size);
    psram_deselect();
}

void psram_load_data(void *buf, uint32_t addr, unsigned int size)
{
    while (size >= 1024)
    {
        psram_access(addr, 1024, true, buf);
        addr += 1024;
        buf += 1024;
        size -= 1024;
    }

    if (size)
        psram_access(addr, size, true, buf);
}
