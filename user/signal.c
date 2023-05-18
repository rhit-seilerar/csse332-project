#include "../kernel/types.h"
#include "user.h"
#include "../kernel/riscv.h"
// #include "defs.h"
#include "../kernel/param.h"
#include "../kernel/spinlock.h"
#include "../kernel/proc.h"
#include "../kernel/signal.h"

// These functions are mapped into userspace 

void handle_signals(void);
SIGNAL_HANDLER(signal_handler_ignore);
SIGNAL_HANDLER(signal_handler_terminate);
#define CATCHABLE_SIGNAL(name, handler)
#define UNCATCHABLE_SIGNAL(name) SIGNAL_HANDLER(signal_handler_##name);
SIGNALS
#undef CATCHABLE_SIGNAL
#undef UNCATCHABLE_SIGNAL

static signaling_t s = {
  .handlers = {
    #define UNCATCHABLE_SIGNAL(name)
    #define CATCHABLE_SIGNAL(name, handler) \
      [SIGNAL_##name] signal_handler_##handler,
    SIGNALS
    #undef CATCHABLE_SIGNAL
    #undef UNCATCHABLE_SIGNAL
  },
  
  .handle_at = {
    .ra = (uint64)handle_signals
  },
  
  .can_interrupt = 1
};

SIGNAL_HANDLER(signal_handler_ignore) {
  return 0;
}

SIGNAL_HANDLER(signal_handler_terminate) {
  return kill(getpid());
}

SIGNAL_HANDLER(signal_handler_KILL) {
  return kill(getpid());
}

void handle_signals(void) {
  for(; s.num_to_handle > 0; s.num_to_handle--) {
    signal_t signal = s.queue[s.read];
    s.read = (s.read+1) % MAX_SIGNALS;
    
    int result;
    switch(signal.type) {
      //
      // Specially handle the uncatchable signals
      //
      #define CATCHABLE_SIGNAL(...)
      #define UNCATCHABLE_SIGNAL(name) \
      case SIGNAL_##name: result = signal_handler_##name(signal); break;
      SIGNALS
      #undef UNCATCHABLE_SIGNAL
      #undef CATCHABLE_SIGNAL
      
      //
      // Dispatch catchable signals
      //
      default: {
        result = s.handlers[signal.type](signal);
      }
    }
    
    if(result) break;
  }
  
  asm volatile(
    "ld ra,    0(%0)\n"
    "ld sp,    8(%0)\n"
    "ld s0,   16(%0)\n"
    "ld s1,   24(%0)\n"
    "ld s2,   32(%0)\n"
    "ld s3,   40(%0)\n"
    "ld s4,   48(%0)\n"
    "ld s5,   56(%0)\n"
    "ld s6,   64(%0)\n"
    "ld s7,   72(%0)\n"
    "ld s8,   80(%0)\n"
    "ld s9,   88(%0)\n"
    "ld s10,  96(%0)\n"
    "ld s11, 104(%0)\n"
    "ret"
  : "=r" (s.return_to));
}