typedef int pid_t;

typedef struct signal {
  int type;
  pid_t sender;
  void *message;
} signal_t;

#define SIGNAL_HANDLER(name) int name(signal_t signal)
typedef int (*signal_handler_t)(signal_t);

//
// X-Macro for defining signals
//
#define SIGNALS \
  CATCHABLE_SIGNAL(ALARM, ignore) /* Timed interrupt */ \
  CATCHABLE_SIGNAL(MESSAGE, ignore) /* Send arbitrary data to another process */ \
  UNCATCHABLE_SIGNAL(KILL) /* Unconditionally kill a process */ \

enum signal_type {
  //
  // List catchable signals
  //
  #define CATCHABLE_SIGNAL(name, handler) SIGNAL_##name,
  #define UNCATCHABLE_SIGNAL(name)
  SIGNALS
  #undef CATCHABLE_SIGNAL
  #undef UNCATCHABLE_SIGNAL
  
  // Number of handlers in the array
  SIGNAL_CATCHABLE_COUNT,
  
  //
  // List uncatchable signals
  // 
  #define CATCHABLE_SIGNAL(name, handler)
  #define UNCATCHABLE_SIGNAL(name) SIGNAL_##name,
  SIGNALS
  #undef CATCHABLE_SIGNAL
  #undef UNCATCHABLE_SIGNAL
  
  // The total number of signal types
  SIGNAL_overshot_count,
  SIGNAL_COUNT = SIGNAL_overshot_count - 1
};

// These are stored in the low 3 bits of the function pointers
enum signal_flags {
  SIGNAL_MASKED      = 0b001,
  SIGNAL_CATCHABLE   = 0b010,
};

#define MAX_SIGNALS (PGSIZE / sizeof(struct signal))
#define MAX_CATCHABLE (PGSIZE / sizeof(signal_handler_t))
_Static_assert(SIGNAL_CATCHABLE_COUNT <= MAX_CATCHABLE, "too many signals are defined");