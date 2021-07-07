#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

// 4.1 bsemaphore
struct bsemaphore bsemaphoreArray[MAX_BSEM]; 

struct proc *initproc;

struct { // locking problems 
  struct spinlock lock;
  struct proc proc[NPROC];
} procTable;

int nextpid = 1;
int nextThreadID = 1; // like nextpid just for threads
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
extern void sigretEnd(void);   //calculate sigret size 
extern void sigretStart(void); //calculate sigret size

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  struct thread *t;
  for (p = procTable.proc; p < &procTable.proc[NPROC]; p++){
    for (t = p->threadArray; t < &p->threadArray[NTHREAD]; t++){
      char *pa = kalloc();
      if (pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int)(p - procTable.proc) , (int)(t - p->threadArray));
      kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
  }
}

void semaphoreInit(void){
  struct bsemaphore* b;
  for (b = bsemaphoreArray; b < &bsemaphoreArray[MAX_BSEM]; b++){
      initlock(&b->lock, "bsemaphore");
      b->state = UNUSED;
      b->bINDEX = 0;
  }
}

// initialize the proc table at boot time.
void procinit(void)
{
  struct thread *t;
  struct proc *p;
  initlock(&procTable.lock,"procTable");
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  semaphoreInit();
  for (p = procTable.proc; p < &procTable.proc[NPROC]; p++){
    initlock(&p->lock, "proc");
    for (t = p->threadArray; t < &p->threadArray[NTHREAD]; t++){
      initlock(&t->lock, "thread");
      t->kstack = KSTACK((int)(p - procTable.proc) , (int)(t - p->threadArray));
    }
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
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

struct thread*
mythread(void){
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->t;
  pop_off();
  return t;
}

int
allocpid() {
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
  struct thread *t;
  int i = 0; // index of thread
  for(p = procTable.proc; p < &procTable.proc[NPROC]; p++) {
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

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
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

  //2.1.2
  p->pendingSignals = 0;
  p->signalMask = 0;
  p->userTrapFrameBackUp = 0;
  p->prevSignalMask = 0;
  p->stop = 0;
  for(int i=0;i<32;i++){
    p->signalHandlers[i] = SIG_DFL; 
  }
  //2.1.2

  struct thread* head = &p->threadArray[0];
  head->trapframe = (struct trapframe*)(p->trapframe); // the first thread gets p's trapframe
  

  acquire(&head->lock); // setting its threadID
  p->threadArray[0].threadID = nextThreadID;
  nextThreadID += 1;
  release(&head->lock);
  
  for(t=p->threadArray; t<&p->threadArray[NTHREAD]; t++){ // setting p->threadArray threads
    t->trapframe = (struct trapframe*)(p->trapframe + i);
    t->userTrapFrameBackUp = (struct trapframe*)(p->userTrapFrameBackUp + i);
    t->p = p;
    t->threadINDEX = i; // we need it to trap.c
    t->state = UNUSED;
    // p->threadArray[0].trapframe + (uint64)(sizeof(struct trapframe)*i);
    //t->userTrapFrameBackUp = p->threadArray[0].userTrapFrameBackUp + (uint64)(sizeof(struct trapframe)*i);
    i += 1;
  }

 
  acquire(&head->lock); 
  memset(&head->context, 0, sizeof(head->context));
  head->context.ra = (uint64)forkret;
  head->context.sp = head->kstack + PGSIZE;


  
  //release(&head->lock);  ???
  //release(&procTable.lock);

  return p;
}

static struct thread*
allocthread(struct proc* p) // gets its main thread
{
  if(p != 0)
    return &p->threadArray[0];
  return 0;
}

static struct thread*
createallocthread(struct proc* p) // allocating thread in p threadArray
{
  struct thread* t;
  acquire(&p->lock);
  int tindex = 0; 
  for(t=p->threadArray; t<&p->threadArray[NTHREAD]; t++){
    if(t->state == UNUSED){
      t->p = p;
      t->threadINDEX = tindex;
      goto found;
    }
    else  
      tindex += 1;
  }
  release(&p->lock);
  return 0;

  found:
    release(&p->lock);
    t->state = USED;
    t->threadID = nextThreadID;
    nextThreadID += 1;
    memset(&t->context, 0, sizeof(t->context));
    t->context.ra = (uint64)forkret;
    t->context.sp = t->kstack + PGSIZE;
  return t;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  struct thread* t;
  for(t=p->threadArray; t < &p->threadArray[NTHREAD]; t++){ // first free threads
    t->trapframe = 0;
    t->userTrapFrameBackUp = 0;
    t->threadID = 0;
    t->p = 0;
    t->chan = 0;
    t->killed = 0;
    t->state = UNUSED;
    t->pendingSignals = 0;
    t->signalMask = 0;
    //t->kstack = 0;
    t->prevSignalMask = 0;
    t->stop = 0;
    t->killed = 0;
    //t->xstate = 0;
  }

  //printf("free process now\n");
  if(p->userTrapFrameBackUp)
    kfree((void*)p->userTrapFrameBackUp);
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  p->userTrapFrameBackUp = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  //p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
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

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
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
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
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
  struct thread* t;
  p = allocproc();
  initproc = p;
  t = &p->threadArray[0];
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.

  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer
  t->trapframe->epc = 0;     // user program counter
  t->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  t->state = RUNNABLE;
  release(&t->lock);

  p->state = RUNNABLE; 
  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
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
  struct thread *nt;

  // Allocate process.
  if((np = allocproc()) == 0 || (nt = allocthread(np)) == 0){
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
  *nt->trapframe = *mythread()->trapframe;
  // Cause fork to return 0 in the child.
  nt->trapframe->a0 = 0;

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
  //2.1.2
  np->signalMask = p->signalMask;
  np->prevSignalMask = p->prevSignalMask; 
  for(int i=0;i<32;i++)
    np->signalHandlers[i] = p->signalHandlers[i];
  release(&np->lock);


  acquire(&np->lock);
  np->state = RUNNABLE;              
  nt->state = RUNNABLE; 
  release(&np->lock);


  release(&nt->lock); // from allocproc

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = procTable.proc; pp < &procTable.proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

void killThreads(struct proc* p){
  acquire(&p->lock);
  struct thread* t;
  for(t = p->threadArray; t < &p->threadArray[NTHREAD]; t++) {
    if(t->state == SLEEPING)
      t->state = RUNNABLE;
    t->killed = 1;
  }
  release(&p->lock);
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();
  struct thread *t = mythread();

  if(p == initproc)
    panic("init exiting");

  killThreads(p); // exit its threads
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
  release(&p->lock);


  acquire(&t->lock);
  t->xstate = status;
  t->state = ZOMBIE;


  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = procTable.proc; np < &procTable.proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
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
  struct thread *t;
  struct proc *p;
  struct cpu *c = mycpu();
  int pIndex = 0; // for debuging and printing
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = procTable.proc; p < &procTable.proc[NPROC]; p++) {
      if(p->state == RUNNABLE) {
        //printf("hello pid %d\n",pIndex);
        for (t = p->threadArray; t<&p->threadArray[NTHREAD];t++){
          acquire(&t->lock);
          if (t->state == RUNNABLE) { // same as with processes
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
            p->state = RUNNABLE;
            c->proc = p;
            t->state = RUNNING;
            c->t = t;

            swtch(&c->context, &t->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
            c->proc = 0;
            c->t = 0;
          }
          release(&t->lock);
        }
      }
      pIndex+=1;
    }
    pIndex = 0;
  }
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
  struct thread *t = mythread();

  if (!holding(&t->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if (t->state == RUNNING && p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct thread *t = mythread();
  acquire(&t->lock);
  //p->state = RUNNABLE;
  t->state = RUNNABLE;
  sched();
  release(&t->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;
  struct thread* t = mythread();
  // Still holding p->lock from scheduler.
  release(&t->lock);

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
  //struct proc *p = myproc();
  struct thread *t = mythread();
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.
  //acquire(&p->lock);  //DOC: sleeplock1
  acquire(&t->lock); //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  //p->chan = chan;
  //p->state = SLEEPING;
  t->chan = chan;
  t->state = SLEEPING;

  sched();

  // Tidy up.
  //p->chan = 0;
  t->chan = 0;

  // Reacquire original lock.
  release(&t->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct thread *t;
  struct proc *p;
  for(p = procTable.proc; p < &procTable.proc[NPROC]; p++) {
    if(p->state == RUNNABLE){
      acquire(&p->lock);
      for (t = p->threadArray; t<&p->threadArray[NTHREAD]; t++){
          if(t != mythread()){ // same as the original if(p != myproc()) .. 
            acquire(&t->lock);
            if(p->state == RUNNABLE && t->state == SLEEPING && t->chan == chan) {
              t->state = RUNNABLE;
            }
            release(&t->lock);
          }
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid, int signum) // update new kill 
{ 
  //printf("got kill call %d\n",signum);
  struct proc *p;
  if(signum >= 0 && signum <= 31){
    for(p = procTable.proc; p < &procTable.proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->pid == pid){
        p->pendingSignals |= (1 << signum);
        release(&p->lock);
        return 0;
      }
    release(&p->lock);
    }
  }
  return -1;
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
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = procTable.proc; p < &procTable.proc[NPROC]; p++){
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


//2.1.3
uint sigprocmask (uint signalMask){
  struct proc *p;
  p = myproc();
  struct thread *t;
  t = mythread();
  acquire(&p->lock);
  uint oldsignalMask = p->signalMask;
  p->signalMask = signalMask;  
  t->signalMask = signalMask;  
  release(&p->lock);
  return oldsignalMask;
}

//2.1.4
int sigaction (int signum, const struct sigaction* act, struct sigaction *oldact){ 
  struct proc *p;
  p = myproc();
  acquire(&p->lock);
  ///Make sure that SIGKILL and SIGSTOP cannot be modified,blocked, or ignored! 
  if(signum == SIGSTOP || signum == SIGKILL){
    release(&p->lock);
    return -1;
  }
  if(signum>31 || signum<0){
    release(&p->lock);
    return -1;
  }
  if(oldact){ // oldact isnt Null
    copyout(p->pagetable, (uint64)oldact,(char*)&p->signalHandlers[signum] , sizeof(struct sigaction));
  }  
  if(act != 0 && copyin(p->pagetable, (char*)&p->signalHandlers[signum], (uint64)act,
                        sizeof(struct sigaction)) < 0) {
      release(&p->lock);
      return -1;
  }
  release(&p->lock);
  return 0;
}

//2.1.5
void sigret(void){
  struct proc *p = myproc();
  struct thread *t = mythread();
  copyin(p->pagetable,(char*)&t->trapframe,
  (uint64)t->userTrapFrameBackUp,sizeof(struct trapframe));
  t->signalMask = t->prevSignalMask; // return to the previous signal mask 
}

//2.3
void handleKill(int i){ // SIGKILL
  struct proc *p = myproc();
  p->killed = 1;
  p->pendingSignals ^= (1<<i);
  if(p->state == SLEEPING){
    // Wake process from sleep().
    p->state = RUNNABLE;
  }
}

void handleStop(int i){ // SIGSTOP
  struct proc *p = myproc();
  p->stop = 1;
  while(p->stop == 1 && (p->pendingSignals & (1<<SIGCONT)) == 0){ // will stop when SIGCONT makes it 0 
    yield();
  }
  p->pendingSignals ^= (1<<i);
}

void handleCont(int i){ // SIGCONT
  struct proc *p = myproc();
  struct thread *t = mythread();
  p->stop = 0;
  p->pendingSignals ^= (1<<i);
  t->stop = 0;
  t->pendingSignals ^= (1<<i);
}


//3.2
int kthreadCreate(void (*start_func)(), void *stack){
  //printf("i am %d creating thread\n",mythread()->threadID);

  struct proc *p = myproc();
  struct thread *t = createallocthread(p);
  if(t == 0)
    return -1;
  *t->trapframe = *mythread()->trapframe;
  t->trapframe->epc = (uint64)start_func;
  t->trapframe->sp = (uint64)stack + MAX_STACK_SIZE - 16;
  //printf("created thread id = %d\n",t->threadID);

  acquire(&t->lock);
  t->state = RUNNABLE; 
  release(&t->lock);
  return t->threadID;
}

int someOneAlive(void){ // returns 1 if found any thread who isnt zombie or unused
  struct thread* t;
  acquire(&myproc()->lock);
  for(t = mythread()->p->threadArray; t < &mythread()->p->threadArray[NTHREAD]; t++) {
      acquire(&t->lock);
      if(t->state != ZOMBIE && t->state != UNUSED){
        //t->state = ZOMBIE; 
        release(&t->lock);
        release(&myproc()->lock);
        return 1;
      }
      release(&t->lock);
  }
  release(&myproc()->lock);
  return 0;
}

//3.2
void kthreadExit(int status){
  //printf("thread %d exiting\n",mythread()->threadID);
  struct thread *t = mythread();

  acquire(&t->lock); // set threads xstate
  t->xstate = status;
  release(&t->lock);

  if (someOneAlive()) {
    acquire(&t->lock);
    t->state = ZOMBIE;
    release(&t->lock);
    wakeup(t);
      
    //printf("threadExit -> sched()\n");
    //printf("tid exiting = %d\n",mythread()->threadID);

    acquire(&t->lock); // before sched..
    sched();
    panic("zombie exit");
  }    
  else{
    exit(status);
  }
  
  return;
}

int kthreadJoin(int thread_id, int *status){
  //printf("join is now running in thread %d with tid %d\n",mythread()->threadID,thread_id);

  struct proc *p = myproc();
  struct thread *t;


  for(t = p->threadArray; t<&p->threadArray[NTHREAD]; t++){
    acquire(&t->lock);
    if(t!=mythread() && t->threadID == thread_id){
      release(&t->lock);
      goto found;
    }
    else  
      release(&t->lock);
  }
  return -1;

  found:
  acquire(&wait_lock); //  like in wait(addr)
  for (;;){
    acquire(&t->lock);
    if(mythread()->killed){ // always check if killed
      release(&t->lock);
      release(&wait_lock);
      return -1;
    }

    if(t->threadID != thread_id){ // No point waiting if changed id (as in wait(addr))
      release(&t->lock);
      release(&wait_lock);
      return -1;
    }

    if (t->state == UNUSED || t->state == ZOMBIE){ //copy status as in original wait(addr)
      if(status != 0 && copyout(p->pagetable, (uint64)status, (char *)&t->xstate,
                                  sizeof(t->xstate)) < 0) {
            release(&t->lock);
            release(&wait_lock);
            return -1;
      }
      release(&t->lock);
      release(&wait_lock);
      return 0;
    }
    release(&t->lock);
    // Wait for a child to exit.
    sleep(t, &wait_lock);  //DOC: wait-sleep
  }
}

//4.1
int bsemAlloc(void){
  struct bsemaphore *b;
  int i = 0; // index of bsemaphore
  for(b = bsemaphoreArray; b < &bsemaphoreArray[MAX_BSEM]; b++) {
    acquire(&b->lock);
    if(b->state == UNUSED) {
      goto found;
    } else {
      i += 1;
      release(&b->lock);
    }
  }
  return -1;

found:
  //p->pid = allocpid();
  b->bINDEX = i;
  b->state = USED;
  b->flag = 1;
  release(&b->lock);
  return b->bINDEX;
}

//4.1
void bsemFree(int descriptor){
  acquire(&bsemaphoreArray[descriptor].lock);
  if(bsemaphoreArray[descriptor].state == UNUSED){
    release(&bsemaphoreArray[descriptor].lock);
    return;
  }
  else
    bsemaphoreArray[descriptor].state = UNUSED;
  release(&bsemaphoreArray[descriptor].lock);
}

//4.1
void bsemDown(int descriptor){
  acquire(&bsemaphoreArray[descriptor].lock);
  if(bsemaphoreArray[descriptor].state == USED){
    //printf("flag = %d\n",bsemaphoreArray[descriptor].flag);
    while(bsemaphoreArray[descriptor].flag == 0)
      sleep(&bsemaphoreArray[descriptor],&bsemaphoreArray[descriptor].lock);
    bsemaphoreArray[descriptor].flag = 0;
  }
  release(&bsemaphoreArray[descriptor].lock);
}

//4.1
void bsemUp(int descriptor){
  acquire(&bsemaphoreArray[descriptor].lock);
  if(bsemaphoreArray[descriptor].state == USED){
    bsemaphoreArray[descriptor].flag = 1;
    release(&bsemaphoreArray[descriptor].lock);
    wakeup(&bsemaphoreArray[descriptor]); // for who sleeps in down
  }
  else
    release(&bsemaphoreArray[descriptor].lock);
}
