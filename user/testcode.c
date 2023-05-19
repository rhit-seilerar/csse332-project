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

int pid;

void killself() {
    if(!(pid = fork())) {
        send_signal(SIGNAL_KILL, getpid());
        sleep(1);
        printf("(killself) Self is alive\n");
        exit(1);
    } else {
        wait(&pid);
    }
}

void killchild() {
    if(!(pid = fork())) {
        sleep(1);
        printf("(killchild) Child is alive\n");
        exit(1);
    } else {
        send_signal(SIGNAL_KILL, pid);
    }
}

void whilekill() {
    send_signal(SIGNAL_KILL, getpid());
    sleep(1);
    while(1) {
        printf("(whilekill) Running while\n");
    }
    exit(1);
}

void simplekill() {
    send_signal(SIGNAL_KILL, getpid());
    sleep(2);
    printf("(simplekill) Got past sleep");
    exit(1);
}

struct test {
    void (*f)(char *);
    char *s;
} tests[] = {
    {simplekill, "simplekill"},
    {whilekill, "whilekill"},
    {killself, "killself"},
    {killchild, "killchild"},
    { 0, 0},
};

//
// drive tests
//

// run each test in its own process. run returns 1 if child's exit()
// indicates success.
int
run(void f(char *), char *s) {
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
        if(xstatus == 1) 
        printf("FAILED\n");
        else
        printf("OK\n");
        return xstatus != 1;
    }
}

int
runtests(struct test *tests, char *justone) {
    for (struct test *t = tests; t->s != 0; t++) {
        if((justone == 0) || strcmp(t->s, justone) == 0) {
        if(!run(t->f, t->s)){
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