#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "signal.h"

SIGNAL_HANDLER(signal_handler_ignore) {
  return 0;
}

SIGNAL_HANDLER(signal_handler_terminate) {
  return 0;
}

SIGNAL_HANDLER(signal_handler_KILL) {
  return 0;
}