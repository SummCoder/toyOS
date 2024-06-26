#include "klib.h"
#include "sem.h"
#include "proc.h"

void sem_init(sem_t *sem, int value) {
  sem->value = value;
  list_init(&sem->wait_list);
}

void sem_p(sem_t *sem) {
  // Lab2-4: dec sem's value, if value<0, add curr proc to waitlist and block it
  TODO();
}

void sem_v(sem_t *sem) {
  // Lab2-4: inc sem's value, if value<=0, dequeue a proc from waitlist and ready it
  TODO();
}

#define USER_SEM_NUM 128
static usem_t user_sem[USER_SEM_NUM] __attribute__((used));

usem_t *usem_alloc(int value) {
  // Lab2-5: find a usem whose ref==0, init it, inc ref and return it, return NULL if none
  TODO();
}

usem_t *usem_dup(usem_t *usem) {
  // Lab2-5: inc usem's ref
  TODO();
}

void usem_close(usem_t *usem) {
  // Lab2-5: dec usem's ref
  TODO();
}


