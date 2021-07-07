#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;
  int signum;
  if(argint(0, &pid) < 0 || argint(1, &signum) < 0)
    return -1;
  return kill(pid,signum); // the new kill system call gets 2 arguments
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


//2.1.3
uint64
sys_sigprocmask(void)
{
  int mask;
  if(argint(0, &mask) < 0)
    return -1;
  return sigprocmask(mask);
}

//2.1.4
uint64
sys_sigaction(void)
{
  uint64 act;
  uint64 oldact;
  int signalNum;
  if(argint(0, &signalNum) < 0 || argaddr(1,&act) < 0 || argaddr(2,&oldact) < 0)
    return -1;
  return sigaction(signalNum,(struct sigaction*)act,(struct sigaction*)oldact);
}

//2.1.5
uint64
sys_sigret(void)
{
  sigret();
  return 0;
}

//3.2
uint64
sys_kthread_create(void) // int kthread_create ( void ( *start_func ) ( ) , void*stack ) ;
{
  //printf("hleloasdflsdafdl\n");
  uint64 start_func;
  uint64 stack;
  if(argaddr(0, &start_func) < 0 || argaddr(1, &stack) < 0)
    return -1;
  int res =  kthreadCreate((void (*)())start_func,(void*)stack);
  //printf("in sys proc tid is %d\n",res);
  return res;
}

//3.2
uint64
sys_kthread_id(void) // int kthread_id();
{
  return mythread()->threadID; // check if to add function?
}

//3.2
uint64
sys_kthread_exit(void) // void kthread_exit(int status);
{
  int status;
  if(argint(0, &status) < 0)
    return -1;
  //printf("hello%s\n","");
  kthreadExit(status);
  return 0; // not reached - as in the usual exit
}

//3.2
uint64
sys_kthread_join(void) // int kthread_join(int thread_id, int* status);
{
  int thread_id;
  uint64 status;

  if(argint(0, &thread_id) < 0 || argaddr(1, &status) < 0)
    return -1;

  return kthreadJoin(thread_id,(int*)status);
}

//4.1
uint64
sys_bsem_alloc(void) // int bsem_alloc(void);
{
  return bsemAlloc();
}

//4.1
uint64
sys_bsem_free(void) // void bsem_free(int); 
{
  int descriptor;
  if(argint(0, &descriptor) < 0)
    return -1; 
  if(descriptor<0 || descriptor>MAX_BSEM)
    return -1;
  bsemFree(descriptor);
  return 0;
}

//4.1
uint64
sys_bsem_down(void) // void bsem_down(int);
{
  int descriptor;
  if(argint(0, &descriptor) < 0)
    return -1; 
  if(descriptor<0 || descriptor>MAX_BSEM)
    return -1;
  bsemDown(descriptor);
  return 0;
}

//4.1
uint64
sys_bsem_up(void) // void bsem_up(int);
{
  int descriptor;
  if(argint(0, &descriptor) < 0)
    return -1; 
  if(descriptor<0 || descriptor>MAX_BSEM)
    return -1;
  bsemUp(descriptor);
  return 0;
}