#include <string.h>
#include "rottr/guest.h"

void rottr_cpu_reset(rottr_cpu *cpu, uint64_t entry, uint64_t stack_top)
{
    memset(cpu, 0, sizeof *cpu);
    cpu->gpr[4] = stack_top; /* RSP */
    cpu->rip = entry;
    cpu->rflags = 0x2;       /* reserved bit, always set */
    cpu->gsBase = 0;         /* GS segment base */
}
