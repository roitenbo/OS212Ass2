struct stat;
struct rtcdate;
struct sigaction;
struct bsemaphore;
struct counting_semaphore;


//2.1.1
#define SIG_DFL 0
#define SIG_IGN 1
#define SIGKILL 9
#define SIGSTOP 17
#define SIGCONT 19

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int,int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
//2.1.3
uint sigprocmask (uint sigmask);

//2.1.4
int sigaction (int signum, const struct sigaction* act, struct sigaction *oldact);

//2.1.5
void sigret (void);

//3.2
int kthread_create (void (*)() , void*);
int kthread_id(void);
void kthread_exit(int);
int kthread_join(int, int*);

//4.1
int bsem_alloc(void);
void bsem_free(int);
void bsem_down(int);
void bsem_up(int);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
