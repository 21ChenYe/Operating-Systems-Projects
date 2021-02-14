/*
Ye Chen
Project 3: Threads
*/

#include <sys/types.h>
#include <unistd.h>
#include <wait.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include "ec440threads.h"
#include <semaphore.h>
#include <stdbool.h>
#include <signal.h>

#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7
#define BYTES 32767

//gcc -Werror -Wall -g -c - o threads.o threads.c

const int READY = 0;
const int RUNNING = 1;
const int EXITED = 2;
const int INTRPT = 3;
const int BLOCKED = 4;
const int SBLOCKED = 5;

int num_threads = 0;

sigset_t sigList;

struct thread{ //double circular linked list!
	pthread_t waitTarget; //blocked, may not need to support >1 in the struct
	pthread_t thread_id;
	int state;
	jmp_buf buf;
	unsigned long int *stack;
	struct thread *next;
	struct thread *prev;
	void *ret_val;
	
};
struct qt{

	struct thread *thread;
	struct qt *next;

};

typedef struct{

	int currValue;
	struct qt *queue; 
	struct qt *last;
	bool initialized;
	
}semaphore;

struct thread *head = NULL; //first thread! usually is main
struct thread *current = NULL; //current thread, used to iterate round robin
struct thread *waitCurrent = NULL;
struct thread *last = NULL;
struct thread *delete = NULL; //schedule for deletion
struct thread *currInsert = NULL;



void lock(){ //test with submit3 even if code fails
	sigemptyset(&sigList);
	sigaddset(&sigList, SIGALRM);
	sigprocmask(SIG_BLOCK, &sigList, NULL);
}
void unlock(){
	sigemptyset(&sigList);
	sigaddset(&sigList, SIGALRM);		
	sigprocmask(SIG_UNBLOCK, &sigList, NULL);
}


void insert(struct thread *tcb){ //create the linked list! 
	num_threads++;
	if(head == NULL){ //if list is empty
		head = tcb;
		head->next = head;
		head->prev = head;
		current = head;
	}else if(last == NULL){ //if list has one element
		head->next = tcb;
		last = head->next;
		last->prev = head;
		last->next = head;	
		head->prev = last;
	}else{ //other cases, will always add to end of list (and then loop back to head!)
		last->next = tcb;
		currInsert = last;
		last = last->next;
		last->next = head;
		last->prev = currInsert;
		head->prev = last;
		
	}
	
}
void schedule(int input){ //input does nothing, simply exists for labeling purposes when called
	//printf("num_threads: %d\n", num_threads);
	
	//if(num_threads == 1 && (int) current->thread_id == 0){
		
	if(!setjmp(current -> buf)){ //if setjmp is 0, then this evaluaates to true, setjmp is only >0 if it has been long jumped
		//printf("State is %d - ID is %d\n", current->state, (int)current->thread_id);
		lock();
		if(current->state == RUNNING){
			current -> state = READY;
			current = current->next;
		}else if(current->state == READY){
			current -> state = RUNNING;
		}
		
		while(current->state == EXITED || current->state == BLOCKED || current->state == SBLOCKED){
			if(current->state == BLOCKED){ //check for the blocked thread's status
				waitCurrent = current;
				//printf("State pre-block checks is %d - ID is %d\n", current->state, (int)current->thread_id);
				while(current->waitTarget != waitCurrent->thread_id){
					waitCurrent = waitCurrent->next;
				//test main thread + 1 thread
				}
				if(waitCurrent->state == EXITED){
					//printf("BLOCK CHECK: waitState is %d - ID is %d\n", waitCurrent->state, (int)waitCurrent->thread_id);
					//printf("SCHEDULE TEST: %lu\n", (long unsigned int) waitCurrent->ret_val);
					current->state = RUNNING;
					break;
				}	
					
			}
			current = current->next;
		}
	//printf("State is %d - ID is %d\n", current->state, (int)current->thread_id);

	//printf("LONG JUMP\n");
	current->state = RUNNING;
	unlock();
	longjmp(current->buf, 1);
	
	}
	

}

void push(sem_t *sem){ //create the linked list! 
	
	semaphore *localSem = (semaphore *) sem->__align;
	struct qt *qthread = (struct qt*) malloc(sizeof(struct qt));
	

	if(localSem->queue == NULL){ //if list is empty
		localSem->queue = qthread;
		localSem->queue->thread = current;
		localSem->queue->next = NULL;
	}else if(localSem->last == NULL){ //if list has one element
		localSem->queue->next = qthread;
		localSem->last = localSem->queue->next;
		localSem->last->thread = current;
		localSem->last->next = NULL;
	}else{ //other cases, will always add to end of list (and then loop back to head!)
		
		localSem->last->next = qthread;
		localSem->last = localSem->last->next;
		localSem->last->thread = current;
		localSem->last->next = NULL;
		
	}
	
} 

void pop(sem_t *sem){
	semaphore *localSem = (semaphore *) sem->__align;
	struct qt *delete;
	
	if(localSem -> queue == NULL){
		return;
	}else{
		delete = localSem->queue;
		localSem->queue->thread->state = READY;
		localSem->queue = localSem->queue->next;
		free(delete);

	}
	
	
}

int sem_init(sem_t *sem, int pshared, unsigned value){
	lock();
	
	semaphore *newSem = (semaphore *) malloc(sizeof(semaphore));
	newSem->initialized = true;
	newSem->queue = NULL; //set queue to null? cuz queue is empty atm
	newSem->currValue = value; 
	sem->__align = (long int) newSem;

	unlock();
	return 0; //success

	//return -1; fail case, but prolly not needed
}

int sem_wait(sem_t *sem){
	lock();
	//easier method: semaphore whatever = sem->align;
		semaphore *localSem = (semaphore *) sem->__align;
	if(localSem -> currValue == 0){
		printf("wait\n");
		current->state = SBLOCKED;
		printf("sblock on %d\n", (int) current->thread_id);
		push(sem);
		unlock();
		schedule(SBLOCKED); //when done scheduling, it returns to here
		lock();
		//reschedule when post is called
		//do NOT decrement outside of this sem_wait, make sure you have a way to decrement when post is called
	}

	localSem -> currValue -= 1;
	//printf("decrement\n");
	unlock();
	return 0;
}

//sem wait
//scheduling balblabla
// sempost -> wakes  that shit up
//scheduliong goes back to that woken up thread eventually
// schedule ends
//back in continuing sem wait



int sem_post(sem_t *sem){ //when a thread woken up by sem post starts running, it needs to decrement and needs to return =*=
	lock();
	
	if(((semaphore *) sem->__align) -> currValue == 0){
		//printf("wake up...?\n");
		pop(sem);
	}
	((semaphore *) sem->__align) -> currValue += 1;
	unlock();

	return 0;
}

int sem_destroy(sem_t *sem){
	lock();
	free((semaphore *)sem->__align);
	free(sem);
	unlock();
	return 0;
}

void sig_handler(int signum){

	if(signum == SIGALRM){
		//printf("INTERRUPT\n");
		schedule(INTRPT);
	}
}

//setjmp current thread is 0 if it hasnt returned
//
void initiate(){
	//check if you should put this in lock or unlock
	signal(SIGALRM, sig_handler);
	ualarm(50000, 50000); //in microseconds! 	

	struct thread *tcb = (struct thread *) malloc(sizeof(struct thread));
	setjmp(tcb->buf);
	tcb->thread_id = (pthread_t) num_threads;
	tcb->state = RUNNING;
	tcb->stack = NULL;
	insert(tcb);

}


void pthread_exit(void *value_ptr){ 
	lock();
	//printf("EXITED %d\n", (int) current->thread_id);
	//num_threads--;
	//printf("NUM THREADS: %d\n", num_threads);
	//printf("EXIT VPTR: %lu \n", (long unsigned) value_ptr);
	current->state = EXITED;
	current->ret_val = value_ptr;
	free(current->stack); // J U S T changed
	unlock();
	schedule(EXITED);
	
	
	__builtin_unreachable();

}

pthread_t pthread_self(void){
	return current->thread_id;	
}

void pthread_exit_wrapper(){
	unsigned long int res;
	asm("movq %%rax, %0\n":"=r"(res));
	//printf("WRAPPER TEST: %lu\n", res);
	pthread_exit((void *) res);
}

int pthread_join(pthread_t thread, void **value_ptr){ //wait for all the threads 
	///use lock and unlock for atomic interruptions, not here!
	lock();
	
	if(thread == current->thread_id){
		return -1;
	}
	waitCurrent = current;	
	current->waitTarget = thread;
	//printf("Current Thread %d has %d as target\n", 

	current->state = BLOCKED;	
	unlock();

	schedule(BLOCKED);
	lock();
	while(thread != waitCurrent->thread_id){
				waitCurrent = waitCurrent->next;
	}	

	if(waitCurrent->thread_id == thread && waitCurrent->state == EXITED){
			//printf("If statement\n");
			if(value_ptr != NULL){
				//printf("JOIN VPTR-1: %lu \n",(long unsigned)  waitCurrent->ret_val);

				*value_ptr = waitCurrent->ret_val;
				
		}
	}

	unlock();
	
	//printf("JOIN VPTR: %lu \n",(long unsigned) *value_ptr);
	return 0;	
}


int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg){
	lock();
	if(num_threads == 0){
		initiate(); 
	}
	struct thread *tcb = (struct thread *) malloc(sizeof(struct thread));
	
	
	tcb->thread_id = (pthread_t) num_threads;
	*thread = tcb->thread_id;
	tcb->state = READY;
	tcb->stack = (unsigned long int*) malloc(BYTES);
 

	unsigned long int *topPtr = tcb->stack + (BYTES/sizeof(unsigned long int)) - 1;
	
	*topPtr = (unsigned long int) pthread_exit_wrapper;
	
	setjmp(tcb->buf);
	
	tcb->buf[0].__jmpbuf[JB_R12] = ((unsigned long int) start_routine);	
	tcb->buf[0].__jmpbuf[JB_R13] = ((unsigned long int) arg);
	tcb->buf[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long int) topPtr);	
	tcb->buf[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int) start_thunk);
	
	insert(tcb);
	unlock();
	return 0;
}

