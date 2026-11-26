#include <stdio.h>
#include "rottr/guest.h"

int main(void)
{
    rottr_cpu cpu;
    rottr_cpu_reset(&cpu, 0x140001000u, 0x7ff000u);
    if (cpu.rip != 0x140001000u || cpu.gpr[4] != 0x7ff000u ||
        cpu.rflags != 0x2 || cpu.gpr[0] != 0) {
        puts("guest reset failed");
        return 1;
    }
    return 0;
}
