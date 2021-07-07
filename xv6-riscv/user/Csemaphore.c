#include "user/Csemaphore.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void csem_down(struct counting_semaphore *sem){ // as in the practical session
    bsem_down(sem->s2);
    bsem_down(sem->s1);
    sem->value -= 1;
    if(sem->value > 0)
        bsem_up(sem->s2);
    bsem_up(sem->s1);
}
void csem_up(struct counting_semaphore *sem){ // as in the practical session
    bsem_down(sem->s1);
    sem->value += 1;
    if(sem->value == 1)
        bsem_up(sem->s2);
    bsem_up(sem->s1);
}

int csem_alloc(struct counting_semaphore *sem, int initial_value){
    if((sem->s1 = bsem_alloc()) < 0 || (sem->s2 = bsem_alloc()) < 0)
        return -1;
    sem->value = initial_value;
    return 0;
}

void csem_free(struct counting_semaphore *sem){
    sem->value = 0; // reset
    bsem_free(sem->s1);
    bsem_free(sem->s2);
}
