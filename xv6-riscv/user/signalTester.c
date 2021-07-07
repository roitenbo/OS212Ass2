#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"


#include "kernel/param.h"
#include "kernel/fs.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

struct sigaction {
  void (*sa_handler) (int);
  uint sigmask;
};


int main()
{

    int i;
    struct sigaction a;
    struct sigaction b = {
      .sa_handler = (void*)SIG_IGN,
      .sigmask = 17
    };

    for(i = 1; i < 5; i++) {
        if(sigprocmask(i) == i-1)
            fprintf(1, "sigprocmask good\n");
        else
            fprintf(1, "sigprocmask failed\n");
    }


    sigaction(17, &b, &a);
    if(a.sa_handler == 0 && a.sigmask == 0)
        fprintf(1, "sigaction good\n");

    sigaction(17, &a, &b);
    if((uint64)b.sa_handler == 1 && b.sigmask == 17)
        fprintf(1, "sigaction good\n");


    sigaction(17, &a, &a);
    if(a.sa_handler == 0 && a.sigmask == 0)
        fprintf(1, "sigaction good\n");


    int pid;

    if((pid = fork()) == 0) {
        while (1) {
            printf("1");
        }
    } 
    else
    {  
        fprintf(2, "\ncont\n");
        sleep(10);
        kill(pid, SIGCONT);
        sleep(20);
        fprintf(2, "\nstop\n");
        sleep(10);
        kill(pid, SIGSTOP);
        sleep(20);
         fprintf(2, "\ncont\n");
        sleep(10);
        kill(pid, SIGCONT);
        sleep(20);
        fprintf(2, "\nstop\n");
        sleep(10);
        kill(pid, SIGSTOP);
        sleep(20);
         fprintf(2, "\ncont\n");
        sleep(10);
        kill(pid, SIGCONT);
        sleep(20);
        fprintf(1, "\nkill!\n");
        sleep(10);
        kill(pid, SIGKILL);
        wait(&pid);
    }

    fprintf(1, "\ttests completeed!\n");

    exit(0);
}