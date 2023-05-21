#include "riscv.h"

# Return from a signal
.section signalsec
.globl signalret
signalret:
        # We jump into yield to get back to the scheduler
        li a7, 22 
        ecall
        # should not get here