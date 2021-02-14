//Ye Chen
//Project 4

#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>

#define HASH_SIZE 128

int initialized = 0;
int num_tls = 0;
unsigned int page_size; //typically 4096 bytes

struct page{ //mmap assigns virtual pages with page granularity
	unsigned long long address;
	int ref_count;
};

typedef struct thread_local_storage
{
	pthread_t tid;
	unsigned int size;
	unsigned int page_num;
	struct page **pages;
}TLS;

struct hash_element{
	pthread_t tid;
	TLS *tls;
	struct hash_element *next;
}; 

struct hash_element* hash_table[HASH_SIZE]; //each element could be a linked list of hash elements
struct hash_element* head;


bool hash_tag[HASH_SIZE] = {false};

void tls_handle_page_fault(int sig, siginfo_t *si, void *context)
{ 
	int p_fault = ((unsigned long long) si ->si_addr) & ~(page_size - 1); //location(s?) of page faults? 

	//brute force check with p_fault, every single page ever, pthread_exit destroys
	int count = num_tls;
	int i, page_i;
	
	while(count > 0){
		if(hash_table[i] != NULL){
			count --;
			for(page_i = 0; page_i < hash_table[i]->tls->page_num; i++){
				if(hash_table[i]->tls->pages[page_i]->address == p_fault){
					pthread_exit(NULL);
				}

			}
		}
		i++;

	}
	
	//else, do this stuff:	
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);
	raise(sig);

}

void tls_protect(struct page *p)
{
	if(mprotect((void *) p->address, page_size, 0)){
		fprintf(stderr, "tls_protect: could not protect page \n");
		exit(1);
	}
}

void tls_unprotect(struct page *p)
{
	if(mprotect((void *) p->address, page_size, PROT_READ | PROT_WRITE)){
		fprintf(stderr, "tls_unprotect: could not unprotect page\n");
		exit(1);
	}
}



void tls_init()
{
	struct sigaction sigact;
	page_size = getpagesize();

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = tls_handle_page_fault;

	sigaction(SIGBUS, &sigact, NULL);
	sigaction(SIGSEGV, &sigact, NULL);

	initialized = 1;

}

//pthread_id of 129 and 1 hash to the same place, so the same place should be a linked list including both

int tls_create(unsigned int size){ //size == bytes
	int i = 0, hash;
	
	if(!initialized){
		tls_init();
	}

	hash = pthread_self()%128;

	//Error Handling: check if current thread has a TLS, check size > 0 or not
	if(!(size > 0)){
		return -1;
	} 
	else if(hash_table[hash] != NULL){
		head = hash_table[hash];
		if(head->tid == pthread_self()){
				return -1;
		}

		while(head->next != NULL){
			if(head->tid == pthread_self()){
				return -1;
			}
			head = head->next;
		}
	}
	
	TLS *newTLS; 
	newTLS = (TLS*) calloc(1, sizeof(TLS));
	newTLS->tid = pthread_self();
	newTLS->size = size;
	newTLS->page_num = size/page_size + ((size%page_size) != 0); //I should ceil it
	newTLS->pages = (struct page **) calloc(newTLS->page_num, sizeof(struct page*));
	for(i = 0; i < newTLS->page_num; i++){
		struct page *newPage;
		
		newPage = (struct page*) calloc(1, sizeof(struct page));
		
		newPage->address = (unsigned long long) mmap(NULL, size, 0, MAP_ANON | MAP_PRIVATE, 0, 0);
		newPage->ref_count = 1;
		
		newTLS->pages[i] = newPage;
		
		tls_protect(newTLS->pages[i]);
	}

	num_tls++;
	
	struct hash_element* newHash = (struct hash_element *) malloc(sizeof(struct hash_element));
	newHash->tid = pthread_self();
	newHash->tls = newTLS;
	newHash->next = NULL;
	if(hash_table[hash] == NULL){
		hash_table[hash] = newHash;
	}else{
		head = hash_table[hash];
		while(head->next != NULL){
			head = head->next;
		}
		head->next = newHash;
	}
	return 0;	
}

int tls_write(unsigned int offset, unsigned int length, char *buffer){
	int hash = pthread_self()%128;
	int i = 0, idx, cnt;
	char* dst;
	bool LSA = false;	

	if(hash_table[hash] == NULL){
		return -1;
	}else{
		head = hash_table[hash];
		while(head != NULL){
			if(head->tid == pthread_self()){
				LSA = true;
				break;
			}
			head = head -> next;
		}
	}
	
	if(!LSA){
		//printf("fail1\n");
		return -1;
	}else if(LSA){
		if(offset + length > head->tls->size){
			//printf("fail2\n");
			return -1;
		}
	}

	
	for(i = 0; i < head->tls->page_num; i++){ //unprotect pages
		tls_unprotect(head->tls->pages[i]);
	}

	for (cnt= 0, idx= offset; idx< (offset + length); ++cnt, ++idx) { //memcopy
		struct page *p, *copy;
		unsigned int pn, poff;
		pn= idx/ page_size;
		poff= idx% page_size;
		p = head->tls->pages[pn];
		if (p->ref_count> 1) {
			/* this page is shared, create a private copy (COW) */
			copy = (struct page *) calloc(1, sizeof(struct page));
			copy->address = (unsigned long long) mmap(0, page_size, PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
			copy->ref_count= 1;
			head->tls->pages[pn] = copy;   
			memcpy((void *)copy->address, (void *)p->address, page_size);  
			/* update original page */ 
			p->ref_count--;
			tls_protect(p);
			p = copy;
		}
		dst = ((char *) p->address) + poff; 
		*dst= buffer[cnt];
	}

	for(i = 0; i < head->tls->page_num; i++){ //protect pages
		tls_protect(head->tls->pages[i]);
	}

	return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer){
	int i = 0, idx, cnt;
	bool LSA = false;
	int hash = pthread_self()%128;

	if(hash_table[hash] == NULL){
		return -1;
	}else{
		head = hash_table[hash];
		while(head != NULL){
			if(head->tid == pthread_self()){
				LSA = true;
				break;
			}
			head = head -> next;

		}
	}
	
	if(!LSA){
		return -1;
	}else if(LSA){
		if(offset + length > head->tls->size){
			return -1;
		}
	}

	for(i = 0; i < head->tls->page_num; i++){ //unprotect pages
		tls_unprotect(head->tls->pages[i]);
	}
	for (cnt= 0, idx= offset; idx< (offset + length); ++cnt, ++idx) {
		struct page *p;
		unsigned int pn, poff;
		pn = idx/page_size;
		poff= idx%page_size;
		p = head->tls->pages[pn];
		char * src = ((char *) p->address) + poff;
		buffer[cnt] = *src;
	}

	for(i = 0; i < head->tls->page_num; i++){ //unprotect pages
		tls_protect(head->tls->pages[i]);
	}

	return 0;
	
}

int tls_destroy(){
	int i = 0;
	int hash = pthread_self()%128;
	bool LSA = false;
	struct hash_element* forehead;
	

	if(hash_table[hash] == NULL){
		return -1;
	}else{
		head = hash_table[hash];
		forehead = hash_table[hash];
		while(head != NULL){
			if(head->tid == pthread_self()){
				LSA = true;
				break;
			}
			head = head -> next;
			if(forehead->next != head && forehead->next != NULL){
				forehead = forehead->next;
			}
		}
	}
	
	if(!LSA){
		return -1;
	}
	
	for(i = 0; i < head->tls->page_num; i++){ //check pages
		if(head->tls->pages[i]->ref_count == 1){
			free(head->tls->pages[i]);
		}else if(head->tls->pages[i]->ref_count > 1){
			head->tls->pages[i]->ref_count --;
		}
	}
	forehead->next = head->next;
	head->tid = -1;
	free(head->tls);
	

	num_tls--;
	return 0;

}

int tls_clone(pthread_t tid){
	int i = 0;
	bool t_LSA = false;	
	int hash = pthread_self()%128;
	int t_hash = tid%128;
	if(hash_table[hash] == NULL){ //check current thread
		return -1;
	}else{
		head = hash_table[hash];
		while(head != NULL){
			if(head->tid == pthread_self()){
				return -1;
			}
			head = head -> next;
		}
	}
	
	if(hash_table[t_hash] == NULL){ //check target thread
		return -1;
	}else{
		head = hash_table[t_hash];
		while(head != NULL){
			if(head->tid == tid){
				t_LSA = true;
				break;
			}
			head = head -> next;
		}
	}
	if(t_LSA == false){
		return -1;
	}

	TLS *newTLS; 
	newTLS = (TLS*) malloc(sizeof(TLS));

	newTLS->tid = pthread_self();
	newTLS->size = head->tls->size;
	newTLS->page_num = head->tls->page_num; 
	newTLS->pages = (struct page **) malloc(newTLS->page_num * sizeof(struct page *));

	for(i = 0; i < head->tls->page_num; i++){ //copy pages
		head->tls->pages[i]->ref_count++;
		newTLS->pages[i] = head->tls->pages[i];		
	}

	num_tls++;

	struct hash_element* newHash = (struct hash_element *) malloc(sizeof(struct hash_element));
	newHash->tid = pthread_self();
	newHash->tls = newTLS;
	newHash->next = NULL;
	if(hash_table[hash] == NULL){
		hash_table[hash] = newHash;
	}else{
		head = hash_table[hash];
		while(head->next != NULL){
			head = head->next;
		}
		head->next = newHash;
	}

	return 0;
}




