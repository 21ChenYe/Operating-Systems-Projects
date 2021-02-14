#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include "disk.h"

#define MAX_FILDES 32
#define MAX_F_NAME 15
#define FREE -1
#define ENDOF -2
#define DATA -3
#define DISK_SIZE 4096*4*1024
#define BLOCK_SIZE 4096

typedef struct{
	int fat_idx; //first block of the FAT
	int fat_len; //length of FAT
	int dir_idx; //first block of directory
	int dir_len; //length of directory in blocks
	int data_idx; //first block of file-data
	int tot_files; //total files
}super_block;

typedef struct{
	bool used;
	char name [MAX_F_NAME + 1];
	int size;
	int head;
	int ref_cnt;
}dir_entry;

typedef struct{
	bool used;
	int file;
	int offset;
}file_descriptor;

super_block* fs;
file_descriptor fildes[MAX_FILDES];
int *FAT;
dir_entry *DIR;

int fildes_cnt;

int make_fs(char *disk_name){
	int i = 0;	

	if(make_disk(disk_name) == -1){
		return -1;
	}
	
	if(open_disk(disk_name) == -1){
		return -1;
	}
	
	//initialize data structures
	fs = calloc(1, sizeof(super_block)); 
	DIR = calloc(64, sizeof(dir_entry)); //64 files max at any given time
	FAT = calloc(8192, sizeof(int)); //chars are ints, 4096 blocks for metadata section

	//SUPER BLOCK SETUP
	fs->fat_idx = 1;
	fs->fat_len = 8192*(sizeof(int))/BLOCK_SIZE; //entries * size of int / block_size = 4
	fs->dir_idx = fs->fat_len + 1; //5
	fs->dir_len = 1; //one block for directory
	fs->data_idx = fs->dir_idx + fs->dir_len; //6
	fs->tot_files = 0;

	if(block_write(0, (char *) fs) == -1){
		return -1;
	}
	
	//DIR SETUP
	if(block_write(fs->dir_idx, (char*) DIR) == -1){
		return -1;
	}
	
	//FAT STRUCTURE SETUP
	/*for(i = 0; i < 4; i++){
		FAT[i] = DATA;
	}*/
	
	for(i = 0; i < 8192; i++){ //set everything to FREE
		FAT[i] = FREE;
	}

	for(i = 0; i < fs->fat_len; i++){
		if(block_write(fs->fat_idx + i, (char *) (FAT + i*512)) == -1){
			return -1;
		}
	}

	free(fs);
	free(DIR);
	free(FAT);

	if(close_disk() == -1){
		return -1;
	}
	
	return 0;

}

int mount_fs(char *disk_name){
	fildes_cnt = 0;
	int i;
	
	if(open_disk(disk_name) == -1){
		return -1;
	}
		
	fs = calloc(1, sizeof(super_block)); 
	DIR = calloc(64, sizeof(dir_entry)); //64 files max at any given time
	FAT = calloc(8192, sizeof(int)); //chars are ints, 4096 blocks for metadata section

	if (block_read(0, (char*) fs) == -1) { //load super block
    		return -1;
  	}

	if(block_write(fs->dir_idx, (char*) DIR) == -1){//load directory
		return -1;
	}else{ 
		for(i = 0; i < 64; i++){
			DIR[i].ref_cnt = 0;
			DIR[i].used = false;
		}	
	}
	
	for(i = 0; i < fs->fat_len; i++){ //load FAT
		if(block_read(fs->fat_idx + i, (char *) (FAT+i*512)) == -1){
			return -1;
		}
	}

	for (i = 0; i < MAX_FILDES; i++) {
		fildes[i].used = false;
		fildes[i].file = FREE;
		fildes[i].offset = 0;
	}
	
	return 0;
}


int umount_fs(char *disk_name){
	
	int i;

	if (block_write(0, (char*) fs) < 0) {
		return -1;
	}

	if(block_write(fs->dir_idx, (char*) DIR) == -1){
		return -1;
	}

	for(i = 0; i < fs->fat_len; i++){
		if(block_write(fs->fat_idx + i, (char *) (FAT+i*512)) == -1){
			return -1;
		}
	}

	if(close_disk() == -1){
		return -1;
	}
	free(fs);
	free(DIR);
	free(FAT);
	
	return 0;

}


int fs_open(char *name){
	int i, y;
	if(fs->tot_files == 0 || fildes_cnt == MAX_FILDES){
		return -1;
	}		
	
	for(i = 0; i < 64; i++){
		if (strcmp(DIR[i].name, name) == 0) { //file exists
			for(y = 0; y < MAX_FILDES; y++){
				if(fildes[y].used == false){ //find empty slot
					fildes[y].used = true;
					fildes[y].file = DIR[i].head;
					fildes[y].offset = 0;
					DIR[i].ref_cnt++;
					fildes_cnt++;
					return y;
	
				}
			}
		}
	}
	return -1;

}


int fs_close(int fd){
	int i;
	if(fd < 0 || fd >=MAX_FILDES){
		return -1;
	}else if(fildes[fd].used == false){
		return -1;
	}else if(fildes_cnt == 0){
		return -1;
	}
	
	for(i = 0; i < 64; i++){
		if(DIR[i].head == fildes[fd].file){
			DIR[i].ref_cnt--;
			fildes[fd].used = false;
			fildes[fd].file = FREE;
			fildes[fd].offset = 0;
			fildes_cnt--;
			return 0;
		}
	}
	
	return -1;

}


int fs_create(char *name){
	int i, blocknum;
	blocknum = -1;
	if(strlen(name) > MAX_F_NAME){//error checking
		return -1;
	}else if(fs->tot_files == 64){
		return -1;
	}else{
		for(i = 0; i < 64; i++){
			if(DIR[i].used == true){
				if(strcmp(DIR[i].name, name) == 0){
					return -1;
				}
			}
		}
	}
	for(i = fs->data_idx; i < 8192; i++){
		if(FAT[i] == FREE){
			FAT[i] = ENDOF;
			blocknum = i;
			break;
		}
	}
	if(blocknum == -1){
		return blocknum;
	}
	for(i = 0; i < 64; i++){
		if(DIR[i].used == false){
			DIR[i].used = true;
			DIR[i].size = 0;
			DIR[i].head = blocknum;
			DIR[i].ref_cnt = 0;
			memcpy(DIR[i].name, name, strlen(name));
			fs->tot_files++;
			return 0;
		}
	}
	return -1;

}


int fs_delete(char *name){
	int i, fblock, nblock;
	if(fs->tot_files == 0){
		return -1;
	}else if(strlen(name) > MAX_F_NAME+1){
		return -1;
	}
		
	for(i = 0; i < 64; i++){
		if(strcmp(DIR[i].name, name) == 0){
			if(DIR[i].ref_cnt != 0){
				return -1;
			}else{
				fblock = DIR[i].head;
				while(FAT[fblock] != FREE){
					nblock = FAT[fblock];
					FAT[fblock] = FREE;
					if(nblock == ENDOF){
						break;
					}
					fblock = nblock;
				}
				

			}
		}
	}
	DIR[i].used = false;
	memset(DIR[i].name, 0, strlen(DIR[i].name));
	DIR[i].size = FREE;
	DIR[i].head = FREE;
	fs->tot_files--;
	return 0;

}

int fs_get_filesize(int desc){
	if (fildes[desc].used == false) {
		//printf("Error: File not open\n");
		return -1;
	}

	int i;
	for (i = 0; i < 64; i++) {
		if (DIR[i].head == fildes[desc].file) {
			return DIR[i].size;
		}
	}
	
	return -1;

}


int fs_read(int fd, void *buf, size_t nbytes){
	
	int i, dir_num, read_len;
	bool found = false;
	
	//error checking
	
	if(fd >= MAX_FILDES || fd < 0){
		//printf("Invalid file descriptor or size\n");
		return -1;
	}else if(fildes[fd].used == false){
		//printf("Invalid file descriptor\n");
		return -1;
	}else if(nbytes <= 0){
		return 0;
	}
	
	//find file in question
	for(i = 0; i < 64; i++){
		if(DIR[i].head == fildes[fd].file){
			found = true;
			if(fildes[fd].offset == DIR[i].size){
					return 0;
			}
			dir_num = i;
			break;
		}
	}
	
	if(found == false){
		//printf("File not found\n");
		return -1;
	}
	
	//calculate parameters necessary for read operation
	if(nbytes > DIR[dir_num].size - fildes[fd].offset){
			//the value to return, the amount of bits you must read
			//hypothetically, you should not need to account for ENDOF blocks
			nbytes = DIR[dir_num].size - fildes[fd].offset;
	}
	
	read_len = nbytes;
	//initialize buffer and read operation variables
	char block_state[BLOCK_SIZE]; //state of a current block
	memset(block_state, -1, BLOCK_SIZE);
	char* read_buf = (char *) malloc(read_len * sizeof(char)); //where we will store all blocks
	memset(read_buf, 0, read_len);
	int offset = fildes[fd].offset; //offset
	int block_curr = fildes[fd].file; //initialize current block as first block
	
	int r_iter = 0;
	//move to block where offset exists, update offset to the block that it exists in
	while(offset >= BLOCK_SIZE){
		offset -= BLOCK_SIZE;
		block_curr = FAT[block_curr];
	}
	
	//printf("test\n");
	while(read_len > 0){
		//printf("block_cur is %d\n", block_curr);
		if(block_read(block_curr, block_state) == -1){ //load the block into block_state
			//printf("block read failed\n");
			return -1;
		}
		
		for(i = offset; i < BLOCK_SIZE; i++){
			read_buf[r_iter] = block_state[i];
			//printf("char %c\n", block_state[i]);
			r_iter++;
			read_len--;
			if(read_len == 0){ //if the read length is exhausted, end it. 
				memcpy(buf, read_buf, nbytes);
				return nbytes;
			}
		} //for loop ends, go to next block
		block_curr = FAT[block_curr]; 
		offset = 0; //reset offset since we're in the next block now!
	}
	//this should never be reached

	return -1;
}


int fs_write(int fd, void *buf, size_t nbytes){
	int i, dir_num, write_len;
	//error checking
	if(fd >= MAX_FILDES || fd < 0){
		//printf("Invalid file descriptor or size\n");
		return -1;
	}else if(fildes[fd].used == false){
		//printf("Invalid file descriptor\n");
		return -1;
	}else if(nbytes <= 0){
		return 0;
	}
	
	//find file in question
	dir_num = -1;
	for(i = 0; i < 64; i++){
		if(DIR[i].head == fildes[fd].file){
			dir_num = i;
			break;
		}
	}
	
	if(dir_num == -1){
		//printf("File not found\n");
		return -1;
	}
	
	//calculate parameters necessary for write operation
	int offset = fildes[fd].offset;
	if(offset + nbytes > DISK_SIZE){
		nbytes = DISK_SIZE-offset; //if the file is enormous, then nbytes becomes limited to what is left
	}
	write_len = nbytes;
	
	char block_state[BLOCK_SIZE];
	memset(block_state, -1, BLOCK_SIZE);
	//char write_buf[nbytes];
	//memcpy(write_buf, buf, nbytes);
	int block_curr = fildes[fd].file;
	int w_iter = 0;
	int j, written = 0;
	int prev_block = 0;
	//bool found = false;
	while(offset >= BLOCK_SIZE){
		offset -= BLOCK_SIZE;
		prev_block = block_curr;
		block_curr = FAT[block_curr];
	}
	/*
	if(FAT[block_curr] == ENDOF){ //new stuff just added
			for(j = fs->data_idx; j < 4096; j++){
					if(FAT[j] == FREE){
						found = true;
						FAT[block_curr] = j;
						FAT[j] = ENDOF;
						break;
					}
			}
	}
	*/
	//printf("block_curr is %d\n\n\n", block_curr);
	
	//printf("1block_curr is %d\n", block_curr);
	while(write_len > 0){
		
		if(block_curr == ENDOF){
			for(j = fs->data_idx; j < 8192; j++){
					if(FAT[j] == FREE){
						FAT[prev_block] = j;
						FAT[j] = ENDOF;
						block_curr = FAT[prev_block];
						break;
					}
				}
		//block_curr = FAT[block_curr];
		//printf("block_curr is %d\n\n\n", block_curr);
		}
		//printf("write_len %d \n", write_len);
		
		
		if(block_read(block_curr, block_state) == -1){ //load the block into block_state
			//printf("block_curr is %d\n\n\n", block_curr);
			return -1;
		}
		
		for(i = offset; i < BLOCK_SIZE; i++){
			block_state[i] = *((char *) buf+w_iter);
			//printf("char %c\n", block_state[i]);
			w_iter++;
			write_len--;
			fildes[fd].offset++;
			written++;
			
			if(write_len == 0){ //no more to write! return now!
				if(block_write(block_curr, block_state) == -1){ //load the block into block_state
					//printf("block write failed\n");
					return -1;
				}
				//printf("offset is %d", fildes[fd].offset);
				if(DIR[dir_num].size < fildes[fd].offset){
					DIR[dir_num].size = fildes[fd].offset;
				}
				return written;
			}
		} //for loop ends, go to next block
		if(block_write(block_curr, block_state) == -1){ //load the block into block_state
			//printf("block write failed\n");
			return -1;
		}
		prev_block = block_curr;
		block_curr = FAT[block_curr]; //go to next block
		offset = 0; //reset offset since we're in the next block now!
	}
	//this should never be reached
	
	return -1;
		
		
		
}
	
	

int fs_listfiles(char ***files){
	char** files_list = (char **) malloc(sizeof(char *)); //create files list
	int i;
	int fcnt = 0; //file count

	for(i = 0; i < 64; i++){ 
		if(DIR[i].used == true){ //find if dir entry is used
			*(files_list+fcnt) = DIR[i].name; //add to files list
			fcnt++; //increment files list and size of files list
		}
	}
	*(files_list+fcnt) = NULL;
	*files = files_list;

	return 0;

}


int fs_lseek(int filnum, off_t offset){
	
	if(offset > fs_get_filesize(filnum) || offset < 0){
		return -1;
	}else if(fildes[filnum].used == false){
		return -1;
	}else{
		fildes[filnum].offset = offset;
		return 0;
	}
	return -1;
	
	return 0;

}

int blocknum(int filnum){
	int i = 0, x = 0;
	//int block_curr = fildes[filnum].file;
	for(i = 0; i < 8192; i++){
		if(FAT[i] == ENDOF){
			printf("%d\n", i);
			x++;
			
		}
	}
	return x;
}

int fs_truncate(int fd, off_t length){
	int i = 0, dir_num = 0;
	bool found = false;
	if(fd >= MAX_FILDES || fd < 0){
		return -1;
	}else if(fildes[fd].used == false){
		return -1;
	}
	
	if(length < 0 || length > DISK_SIZE){
		return -1;
	}
	
	for (i = 0; i < 64; i++) {
		if (DIR[i].head == fildes[fd].file) {
			dir_num = i;
			found = true;
			break;
		}
	}
	
	
	if(DIR[dir_num].size < length || found == false){
		return -1;
	}
	if(DIR[dir_num].size == length){
		return 0;
	}
	
	DIR[dir_num].size = length;
	if (fildes[fd].offset > length) {
		fildes[fd].offset = length;
	}
	
	int block_trunc = length/BLOCK_SIZE;
	block_trunc++;
	int block_curr = DIR[dir_num].head;

	for (i = 0; i < block_trunc; i++) {
		block_curr = FAT[block_curr];
	}

	int block_clear = FAT[block_curr];
	FAT[block_curr] = ENDOF; 

	while (block_clear > 0) {
		block_clear = FAT[block_clear];
		FAT[block_clear] = FREE; 
	}
	return 0;

}
