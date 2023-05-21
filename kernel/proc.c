#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#if ENABLE_DEBUG_PROC_PRINT
#define DEBUG_PROC_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PROC_PRINT(...)
#endif

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S
extern char signalret[]; // signal.S

static inline void
__wfi(void)
{
  asm volatile("wfi");
}

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page and signal stack
  if(!(p->trapframe = (struct trapframe *)kalloc()) || !(p->signaling.stack = kalloc())) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  
  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  
  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  #define CATCHABLE_SIGNAL(name, handler) \
    p->signaling.handlers[SIGNAL_##name] = (signal_handler_t)SIGNAL_HANDLER_##handler;
  #define UNCATCHABLE_SIGNAL(name)
  SIGNALS
  #undef CATCHABLE_SIGNAL
  #undef UNCATCHABLE_SIGNAL

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe) kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->signaling.stack) kfree(p->signaling.stack);
  p->signaling.stack = 0;
  if(p->pagetable) proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline, trapframe, and signaling pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
  
  // map the signal return code in signal.S to the page under TRAPFRAME
  if(mappages(pagetable, SIGNALRET, PGSIZE,
              (uint64)signalret, PTE_R | PTE_X | PTE_U) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
  
  // map the signal stack in signal.S to the page under ret
  if(mappages(pagetable, SIGNALSTACK, PGSIZE,
              (uint64)(p->signaling.stack), PTE_R | PTE_W | PTE_U) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmunmap(pagetable, SIGNALRET, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
  
  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmunmap(pagetable, SIGNALRET, 1, 0);
  uvmunmap(pagetable, SIGNALSTACK, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// This is a reduced exit() for exiting from a signal,
// since the proc is already locked and we won't be
// switching to the scheduler (we're already in it!)
void
exit_from_signal(int status, struct proc *p)
{
  if(p == initproc) panic("init exiting");
  
  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

SIGNAL_HANDLER(signal_handler_ignore) {
  DEBUG_PROC_PRINT("(%d:%d) Ignored!\n", cpuid(), myproc()->pid);
  return 0;
}

SIGNAL_HANDLER(signal_handler_terminate) {
  DEBUG_PROC_PRINT("(%d:%d) Terminated!\n", cpuid(), myproc()->pid);
  return -1;
}

SIGNAL_HANDLER(signal_handler_KILL) {
  return -1;
}

// Returns 1 if the process is no longer runnable
int handle_signals(void *kstack, struct proc *p) {
  struct cpu *c = mycpu();
  int cid = cpuid(); (void)cid;
  int killed = 0;
  
  if(p->signaling.count) {
    // Back up old versions of the trapframe, context, and stacks
    // so we can return to the main process seamlessly
    struct trapframe tf = *p->trapframe;
    struct context ctx = p->context;
    uint64 old_kstack = p->kstack;
    p->kstack = (uint64)kstack;
    
    DEBUG_PROC_PRINT("(%d:%d) Entering signaling loop\n", cid, p->pid);
    
    while(p->signaling.count > 0) {
      // Pop the signal from the queue
      signal_t signal = p->signaling.queue[p->signaling.read];
      p->signaling.read = (p->signaling.read+1) % MAX_SIGNALS;
      p->signaling.count--;
      DEBUG_PROC_PRINT("(%d:%d) Handling Signal ID %d\n", cid, p->pid, signal.type);
      
      int result = 0;
      if(signal.type < SIGNAL_CATCHABLE_COUNT) {
        // If we can catch the signal, it can have a custom handler
        // So, we check if it's a predefined handler or a custom one
        switch((uint64)p->signaling.handlers[signal.type]) {
          case SIGNAL_HANDLER_IGNORE: result = signal_handler_ignore(signal); break;
          case SIGNAL_HANDLER_TERMINATE: result = signal_handler_terminate(signal); break;
          default: {
            // This signal has a custom handler, so we need to jump into
            // usermode to handle it. We'll hijack the yield function to
            // release the lock and jump into usertrapret, which will
            // have a modified trapframe to jump into the handler.
            //
            // When the handler returns, it'll yield back into the
            // scheduler so that other signals can be handled.
            
            // Reset and use the dummy kernel stack
            memmove(kstack, (void*)old_kstack, PGSIZE);
            p->context.sp = (uint64)kstack + (p->context.sp - old_kstack);
            
            // Reset the signal stack
            memset(p->signaling.stack, 0, PGSIZE);
            
            // Set up the trapframe to point to the handler
            *p->trapframe = (struct trapframe){
              .epc = (uint64)p->signaling.handlers[signal.type],
              .ra = (uint64)SIGNALRET,
              .sp = (uint64)SIGNALSTACK+PGSIZE,
              .gp = tf.gp,
              .a0 = ((uint64)signal.sender_pid << 32) | signal.type,
              .a1 = signal.payload,
            };
            
            // Hijack yield to return to usermode
            swtch(&c->context, &p->context);
            
            if(p->state == RUNNABLE) {
              // yield sets state to runnable.
              p->state = RUNNING;
              result = p->trapframe->a0;
            } else if(p->state == SLEEPING) {
              // We don't want to sleep in signal handlers, as it would
              // greatly complicate resuming, so we'll just kill it if
              // that happens.
              result = -1;
            }
            
            p->context = ctx;
          }
        }
      } else {
        // We can't catch the signal, so we'll call its dedicated
        // handler.
        switch(signal.type) {
          #define CATCHABLE_SIGNAL(...)
          #define UNCATCHABLE_SIGNAL(name) \
          case SIGNAL_##name: result = signal_handler_##name(signal); break;
          SIGNALS
          #undef UNCATCHABLE_SIGNAL
          #undef CATCHABLE_SIGNAL
        }
      }
      
      // Nonzero handler results indicate an error, so we terminate
      // the process if that happens. Also, if it's been killed, we
      // don't care about handling any other signals.
      DEBUG_PROC_PRINT("(%d:%d) Post-Signal\n", cid, p->pid);
      if(result) exit_from_signal(result, p);
      killed = p->killed || p->state == ZOMBIE;
      if(killed) break;
    }
    
    // Reset the backed up data before returning to the scheduler
    DEBUG_PROC_PRINT("(%d:%d) Exiting signaling loop\n", cid, p->pid);
    *p->trapframe = tf;
    p->kstack = old_kstack;
  }
  
  return killed;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  int cid = cpuid(); (void)cid;
  
  // Dummy kernel stack to not corrupt the main one
  void *kstack = kalloc();
  if(!kstack) panic("scheduler kstack");
  
  // Number of processes that were scheduled in one loop, to know
  // whether we should call 'wfi'.
  int num_run = 0;
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    num_run = 0;

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        num_run++;
        p->state = RUNNING;
        c->proc = p;

        uint64 local_ticks = ticks;
        if (p->alarm_set && p->cycles_at_alarm >= local_ticks) {
          p->alarm_set = 0;
          send_signal((signal_t){.type=SIGNAL_ALARM, .sender_pid=p->pid}, p->pid);
        }
        
        // acquire(&tickslock);
        // int localticks = ticks;
        // release(&tickslock);
        // if (p->alarm_set && p->ticks_at_alarm >= localticks) {
        //   p->alarm_set = 0;
        //   send_signal((signal_t){.type=SIGNAL_ALARM, .sender_pid=p->pid}, p->pid);
        // }
        
        // Go and process any queued signals
        if(!handle_signals(kstack, p)) {
          // Actually run the process
          DEBUG_PROC_PRINT("(%d:%d) Scheduling to %p\n", cid, p->pid, p->context.ra);
          swtch(&c->context, &p->context);
        }
        
        
        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      
      release(&p->lock);
    }

    if (!num_run) {
      __wfi();
    }
  }
  
  kfree(kstack);
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  DEBUG_PROC_PRINT("(%d:%d) Entering sched\n", cpuid(), p->pid);
  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
  DEBUG_PROC_PRINT("(%d:%d) Exiting sched\n", cpuid(), p->pid);
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  DEBUG_PROC_PRINT("(%d:%d) Yielding\n", cpuid(), p->pid);
  sched();
  DEBUG_PROC_PRINT("(%d:%d) Post-Yielding\n", cpuid(), p->pid);
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  
  DEBUG_PROC_PRINT("(%d:%d) Sleeping\n", cpuid(), p->pid);
  sched();
  DEBUG_PROC_PRINT("(%d:%d) Post-Sleeping\n", cpuid(), p->pid);
  
  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        DEBUG_PROC_PRINT("(x:%d) Waking up\n", p->pid);
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

int set_signal_handler(enum signal_type type, signal_handler_t handler) {
  if(type < 0 || type >= SIGNAL_CATCHABLE_COUNT) return 1;
  myproc()->signaling.handlers[type] = handler;
  return 0;
}

int send_signal(signal_t signal, int receiver_pid) {
  struct proc* receiving_proc = 0;
  for (int i = 0; i < NPROC; i++) {
    if (proc[i].pid == receiver_pid) {
      receiving_proc = &proc[i];
    }
  }
  if (receiving_proc == 0) {
    return 2;
  }
  
  if(signal.sender_pid != receiver_pid) {
    acquire(&(receiving_proc->lock));
  }

  if (receiving_proc->signaling.count+1 < MAX_SIGNALS) {
    receiving_proc->signaling.queue[receiving_proc->signaling.write] = signal;
    receiving_proc->signaling.write = (receiving_proc->signaling.write + 1) % MAX_SIGNALS;
    receiving_proc->signaling.count++;
  } else {
    // Queue full, new signal failed to be added
    return 1;
  }

  if(signal.sender_pid != receiver_pid) {
    release(&(receiving_proc->lock));
  }
  
  return 0;
}

int alarm(struct proc *alarmed_proc, unsigned int seconds) {
  int remaining_seconds = 0;
  uint64 cycles_needed = seconds * 10;
  uint64 local_ticks = ticks;
  
  acquire(&(alarmed_proc->lock));
  if (alarmed_proc->alarm_set == 1) {
    if (seconds == 0) {
      alarmed_proc->alarm_set = 0;
    }
    remaining_seconds = alarmed_proc->cycles_at_alarm - local_ticks / 10;
    alarmed_proc->cycles_at_alarm = local_ticks + cycles_needed;
  } else {
    alarmed_proc->alarm_set = 1;
    alarmed_proc->cycles_at_alarm = seconds * 10 + local_ticks;
  }
  release(&(alarmed_proc->lock));
  return remaining_seconds;
}
