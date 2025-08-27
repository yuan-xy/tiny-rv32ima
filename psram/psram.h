#ifndef _PSRAM_H
#define _PSRAM_H

#include "stdbool.h"
#include "stdint.h"

void psram_cmd(uint8_t cmd);
uint8_t psram_read_kgd(void);
uint8_t psram_init(void);
void psram_access(uint32_t addr, unsigned int size, bool write, void *bufP);
void psram_load_data(void *buf, uint32_t addr, unsigned int size);

#endif