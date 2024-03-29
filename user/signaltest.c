#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

int pid;

void killself(void) {
  if((pid = fork()) == 0) {
    send_signal(SIGNAL_KILL, getpid(), 0);
    sleep(2);
    printf("(killself) Fail\n");
    exit(1);
  }
}

void killchild(void) {
  if((pid = fork()) == 0) {
    sleep(2);
    printf("(killchild) Fail\n");
    exit(1);
  } else {
    send_signal(SIGNAL_KILL, pid, 0);
  }
}

SIGNAL_HANDLER(print_message) {
  printf("You got a message from %d: %d\n", signal.sender_pid, signal.payload);
  return 0;
}

SIGNAL_HANDLER(repeated_alarm) {
  printf("Beep beep beep!\n");
  alarm(2);
  return 0;
}

int main(int argc, char **argv) {
  printf("Proc Starting!\n");
  
  killself();
  killchild();
  
  sleep(1);
  
  set_signal_handler(SIGNAL_MESSAGE, print_message);
  send_signal(SIGNAL_MESSAGE, getpid(), 509);
  sleep(2);
  
  alarm(2);
  printf("wake up...\n");
  set_signal_handler(SIGNAL_ALARM, repeated_alarm);
  sleep(4);
  printf("snoozing...\n");
  set_signal_handler(SIGNAL_ALARM, SIGNAL_HANDLER_IGNORE);
  sleep(4);
  alarm(2);
  printf("wake up...\n");
  set_signal_handler(SIGNAL_ALARM, repeated_alarm);
  sleep(4);
  printf("terminating...\n");
  set_signal_handler(SIGNAL_ALARM, SIGNAL_HANDLER_TERMINATE);
  sleep(4);
  
  printf("Proc Exiting!\n");
  exit(0);
}