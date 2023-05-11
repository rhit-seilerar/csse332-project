typedef int pid_t;

typedef struct signal {
  int type;
  pid_t sender;
  void *message;
} signal_t;

#define SIGNAL_HANDLER(name) int name(signal_t signal)
typedef int (*signal_handler_t)(signal_t);

enum signal_type {
  //
  // kernel-to-process
  //
  
  // Timed interrupt
  SIGNAL_ALARM,
  
  
  
  //
  // process-to-process
  //
  
  // Send arbitrary data to another process
  SIGNAL_MESSAGE,
  
  
  
  //
  // uncatchable
  //
  
  // Unconditionally kill a process
  SIGNAL_KILL = (PGSIZE / sizeof(signal_t)),
  
  
  
  // The maximium number of signal types
  SIGNAL_COUNT = SIGNAL_KILL
};

// These are stored in the low 3 bits of the function pointers
enum signal_disposition {
  SIGNAL_MASKED    = 0b001,
  
  SIGNAL_IGNORE    = 0b000,
  SIGNAL_TERMINATE = 0b110,
};

#define MAX_SIGNALS (PGSIZE / sizeof(struct signal))