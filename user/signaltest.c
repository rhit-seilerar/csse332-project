#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

int pid;

void killself(void) {
  if(!(pid = fork())) {
    send_signal(SIGNAL_KILL, getpid());
    sleep(1);
    printf("(killself) Self is alive\n");
    exit(1);
  } else {
    wait(pid)
  }
}

void killchild(void) {
  if(!(pid = fork())) {
    sleep(1);
    printf("(killchild) Child is alive\n");
    exit(1);
  } else {
    send_signal(SIGNAL_KILL, pid);
  }
}

int main(int argc, char **argv) {
  killself();
  killchild();
  
  exit(0);
}