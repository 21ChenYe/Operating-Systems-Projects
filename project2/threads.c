/*
Ye Chen
Project 2: Threads
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
//#define THREAD_CNT 128
#define BYTES 32767

//gcc -Werror -Wall -g -c - o threads.o threads.c

const int READY = 0;
const int RUNNING = 1;
const int EXITED = 2;
const int INTRPT = 3;


int num_threads = 0;



struct thread{ //double circular linked list!
	pthread_t thread_id;
	int state;
	jmp_buf buf;
	unsigned long int *stack;
	struct thread *next;
	struct thread *prev;
};

struct thread *head = NULL; //first thread! usually is main
struct thread *current = NULL; //current thread, used to iterate round robin
struct thread *last = NULL;
struct thread *delete = NULL; //schedule for deletion

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
		current = last;
		last = last->next;
		last->next = head;
		last->prev = current;
		head->prev = last;
	}
	current = head;
}

void schedule(int input){
	//printf("SCHEDULE\n");
	//saves context 
	//find next ready
	//assign next correct thread here
	
	if(!setjmp(current->buf) && input == INTRPT){ //setjmp returns 0 normally, will return >0 if longjmp has occured for this buffer
		current->state = READY;
		current = current->next;
		current->state = RUNNING;
		longjmp(current->buf, 1);
	}else if(!setjmp(current->buf) && input == EXITED){
		current = current->next;
		current->state = RUNNING;
		
		if(num_threads > 1){ 
			//printf("delete id: %d\n", (unsigned int)delete->thread_id);
			free(delete);
		}
		longjmp(current->buf,1);
	}
	
}
void sig_handler(int signum){
	//printf("ALARM\n");
	if(signum == SIGALRM){
		schedule(INTRPT);
	}

}

//setjmp current thread is 0 if it hasnt returned
//
void initiate(){
	signal(SIGALRM, sig_handler);
	ualarm(50000, 50000); //in microseconds! 

	struct thread *tcb = (struct thread *) malloc(sizeof(struct thread));
	setjmp(tcb->buf);
	tcb->thread_id = (pthread_t) 0;
	tcb->state = RUNNING;
	tcb->stack = NULL;
	insert(tcb);

}


void pthread_exit(void *value_ptr){
	
	//printf("PTHREAD_EXIT\n");

	
	delete = current;
	free(delete->stack);
	delete->prev->next = delete->next;
	delete->next->prev = delete->prev;
	
	
	num_threads--;

	schedule(EXITED);
	__builtin_unreachable();

}

pthread_t pthread_self(void){
	return current->thread_id;	
}
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg){
	
	if(num_threads == 0){
		initiate(); 
	}
	//printf("ID: %x\n", (unsigned int) *thread);
	//TCB 
	struct thread *tcb = (struct thread *) malloc(sizeof(struct thread));
	tcb->thread_id = (pthread_t) num_threads;
	//printf("TID: %ld\n", pthread_self());
	tcb->state = READY;
	tcb->stack = (unsigned long int*) malloc(BYTES);
 

	unsigned long int *topPtr = tcb->stack + (BYTES/sizeof(unsigned long int)) - 1;
	
	*topPtr = (unsigned long int) pthread_exit;
	
	setjmp(tcb->buf);
	
	tcb->buf[0].__jmpbuf[JB_R12] = ((unsigned long int) start_routine);	
	tcb->buf[0].__jmpbuf[JB_R13] = ((unsigned long int) arg);
	tcb->buf[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long int) topPtr);	
	tcb->buf[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int) start_thunk);
	
	insert(tcb);
	return 0;
}
