#ifndef _EMULATOR_H
#define _EMULATOR_H

#include <stdint.h>

enum emulatorCode
{
    EMU_POWEROFF,
    EMU_HIBERNATE,
    EMU_REBOOT,
    EMU_GET_SD,
    EMU_RUNNING,
    EMU_UNKNOWN
};

int start_vm(int prev_power_state);
void vm_init_hw(void);
uint8_t vm_get_powerstate(void);
uint8_t vm_save_powerstate(uint8_t state);

#endif