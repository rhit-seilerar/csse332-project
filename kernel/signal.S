#include "riscv.h"

# Return from a signal
.globl signalret
signalret:
        # a2 is set to zero when the signal begins, so we'll
        # use it as the return register
        mv a2, a0
        # We jump into yield to get back to the scheduler
        li a7, 22
        ecall
        # should not get here