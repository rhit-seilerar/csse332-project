#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
#include "kernel/signal.h"

// Basis for this file was taken from the usertests.c file.

int pid;

void simplekill() {
    send_signal(SIGNAL_KILL, getpid(), 0);
    sleep(2);
    printf("(simplekill) Got past sleep");
    exit(1);
}

void whilekill() {
    send_signal(SIGNAL_KILL, getpid(), 0);
    // sleep(1);
    while(1) {
        printf("(whilekill) Running while\n");
    }
    exit(1);
}

void killchild() {
    if(!(pid = fork())) {
        sleep(1);
        printf("(killchild) Child is alive\n");
        exit(1);
    } else {
        send_signal(SIGNAL_KILL, pid, 0);
    }
}

void killself() {
    if(!(pid = fork())) {
        send_signal(SIGNAL_KILL, getpid(), 0);
        sleep(1);
        printf("(killself) Self is alive\n");
        exit(1);
    } else {
        wait(&pid);
    }
}

SIGNAL_HANDLER(print_message) {
    printf("You got a message from %d: %d\n", signal.sender_pid, signal.payload);
    return 0;
}

void customsignal() {
    set_signal_handler(SIGNAL_MESSAGE, print_message);
    send_signal(SIGNAL_MESSAGE, getpid(), 509);
    sleep(4);
    
    exit(1);
}

SIGNAL_HANDLER(simple_alarm) {
    printf("(simple alarm) Alarm has been received.\n");
    yield();
    return 0;
}

void simplealarm() {
    set_signal_handler(SIGNAL_ALARM, simple_alarm);
    alarm(1);
    sleep(4);
    exit(1);
}

SIGNAL_HANDLER(while_alarm) {
    printf("(while alarm) Alarm has been received.\n");
    yield();
    return 0;
}

void whilealarm() {
    set_signal_handler(SIGNAL_ALARM, simple_alarm);
    alarm(1);
    while(1) {}
    exit(1);
}

void printaddress() {
    printf("\n\n%p\n",&simplekill);
    printf("%p\n",&whilekill);
    printf("%p\n",&killself);
    printf("%p\n",&killchild);
    printf("%p\n",&customsignal);
    printf("%p\n",&simplealarm);
    printf("%p\n\n",&whilealarm);
}

void fullqueue() {
    int count = 1;
    while(!send_signal(SIGNAL_ALARM, getpid(), 0)) {
        count++;
    }
    printf(" (Full queue count = %d)  ", count);
    exit(0);
}

void falsesignal() {
    int out;
    out = send_signal(SIGNAL_ALARM, 1, 0);
    printf("%d\n", out);
    exit(out != 2);
}

struct test {
    void (*f)(char *);
    char *s;
    int e;
} tests[] = {
    {simplekill, "simplekill", -1},
    {whilekill, "whilekill", -1},
    // {killself, "killself"},
    // {killchild, "killchild"},
    // {fullqueue, "fullqueue"},
    // {falsesignal, "falsesignal"},
    // {customsignal, "customsignal"},
    // {simplealarm, "simplealarm"},
    // {whilealarm, "whilealarm"},
    // {printaddress,"printaddress"},
    {0},
};

//
// drive tests
//

// run each test in its own process. run returns 1 if child's exit()
// indicates success.
int
run(void f(char *), char *s, int expected_xstatus) {
    int pid;
    int xstatus;

    printf("test %s: ", s);
    if((pid = fork()) < 0) {
        printf("runtest: fork error\n");
        exit(1);
    }
    if(pid == 0) {
        f(s);
        exit(0);
    } else {
        wait(&xstatus);
        if(xstatus != expected_xstatus) printf("FAILED\n");
        else printf("OK\n");
        return xstatus == expected_xstatus;
    }
}

int
runtests(struct test *tests, char *justone) {
    for (struct test *t = tests; t->s != 0; t++) {
        if((justone == 0) || strcmp(t->s, justone) == 0) {
        if(!run(t->f, t->s, t->e)){
            printf("SOME TESTS FAILED\n");
            return 1;
        }
        }
    }
    return 0;
}

int
drivetests(int continuous, char *justone) {
    do {
        printf("usertests starting\n");
        if (runtests(tests, justone)) {
        if(continuous != 2) {
            return 1;
        }
        }
    } while(continuous);
    return 0;
}

int
main(int argc, char *argv[])
{
    int continuous = 0;
    char *justone = 0;

    if(argc == 2 && strcmp(argv[1], "-c") == 0){
        continuous = 1;
    } else if(argc == 2 && strcmp(argv[1], "-C") == 0){
        continuous = 2;
    } else if(argc == 2 && argv[1][0] != '-'){
        justone = argv[1];
    } else if(argc > 1){
        printf("Usage: usertests [-c] [-C] [-q] [testname]\n");
        exit(1);
    }
    if (drivetests(continuous, justone)) {
        exit(1);
    }

    printf("ALL TESTS PASSED\n");
    exit(0);
}