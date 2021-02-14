#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

int pipe_line(int pipeCount, char *inputParsed[256][256], bool lArrow, bool amper){

	int i, in = 0, fd[2], status;
	pid_t pid;
	
	for(i = 0; i <= (pipeCount-1); i++){ //pipe loop!
		if(lArrow && i == 1){//need to skip filename if '<' exists, since it is saved in datastructure
			i++;	
		}
		pipe(fd);
		if(amper){ //kill zombies
			signal(SIGCHLD, SIG_IGN);
		}
		if((pid = fork()) > 0){ //parent
			if(!amper){
				waitpid(pid, &status, 0); //only wait if & DNE
			}
		}
		else if(pid == 0){ //child
			if (in != 0){
				dup2(in, 0);
				close(in);
			}
			if (fd[1] != 1){
				dup2(fd[1], 1);
				close(fd[1]);
			}
		
		execvp(inputParsed[i][0], inputParsed[i]);
		}
		else if (pid < 0){ //failed to fork... shouldn't happen
			return -1;
		}		

		close(fd[1]); //don't need this anymore 
		in = fd[0]; //save the read end of pipe! need it so next pipe can read from old pipe

	}
	if(in != 0){
		dup2(in, STDIN_FILENO);
	}
	return status;
}


int process(char *input, char *inputParsed[256][256], int argCount){
	bool lArrow = strchr(input, '<') != NULL;
	bool rArrow = strchr(input, '>') != NULL;
	bool pipe = strchr(input, '|') != NULL;
	bool amper = strchr(input, '&') != NULL;
	int i = 0, pipeCount = 0, out;
	
	int fd_in, fd_out, execNum = 0, status;
	pid_t pid;

	if(lArrow){ //handle < character
		
		execNum = 0;
		if(inputParsed[1][1] != NULL){
			perror("ERROR: invalid options");
			return -1;
		}
		
		fd_in = open(inputParsed[1][0], O_RDONLY);
		if(fd_in == -1){
			printf("ERROR: %s\n", strerror(errno));
			printf("String(%s)\n", inputParsed[1][0]);
			return -1;
		}
		dup2(fd_in, STDIN_FILENO);
		close(fd_in);
		

	}

	if(pipe){
		while(input[i] != '\0'){ //count pipes!
			if(input[i] == '|'){
				pipeCount++;
			}
			i++;
		}
		status = pipe_line(pipeCount, inputParsed, lArrow, amper);
		if(status == -1){ 
			printf("ERROR: %s\n", strerror(errno));
			return -1;
		}
		execNum += pipeCount;
		if(lArrow){
			execNum++; //change final execvp, needs to be command right after final | character
		}
		
	}
	

	if(rArrow){ //account for > meta character
	
		if(inputParsed[argCount-1][1] != NULL){
			perror("ERROR: invalid options");
			return -1;
		}
		fd_out = creat(inputParsed[argCount-1][0], 0644); //create file descriptor
		if(fd_out == -1){ 
			printf("ERROR: %s\n", strerror(errno));
			return -1;
		}
		dup2(fd_out, STDOUT_FILENO);
		close(fd_out);

	}
	if(amper){
		signal(SIGCHLD, SIG_IGN); //used b/c we don't wait when & is used
	}
	if((pid = fork()) > 0){ //parent
		if(!amper){
		waitpid(pid, &status, 0);	
		}
	}
	else if(pid == 0){//child
		status = execvp(inputParsed[execNum][0], inputParsed[execNum]);
		
		if(status == -1){
			printf("ERROR: %s\n", strerror(errno));
			
		}
	}
	else if(pid < 0){
		printf("ERROR: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}


int main(int argc, char **argv){
	pid_t pid;
	int status;
	char input[512];
	char input2[512];
	char buff[64];
	char *argsFull[256];
	
	int argCount, stdin_save, stdout_save;
	bool suppress = false;
	const char s[5] = " <>|&";
	const char meta[4] = "<>|&";

	char *token;
	char *token2;
				
	int i = 0, i2 = 0, i3 = 0;	
										
	if(argv[1] != NULL){
		suppress = (strcmp(argv[1], "-n") == 0); //if -n flag is used, make suppress true
	}
	stdin_save = dup(STDIN_FILENO);
	stdout_save = dup(STDOUT_FILENO);
	while(true){
		char* argsParsed[256][256];
		i = 0;

		if(!suppress){
			printf("my_shell$");
		}
		fgets(input, 512, stdin);	
		input[strlen(input)-1] = '\0';
		strcpy(input2, input);
		
		if(feof(stdin)){
			break;
		}							
		
		//lots of string parsing!

		token = strtok(input, meta); 
		while(token != NULL){
			//printf("Token: %s\n", token);
			argsFull[i] = token;
			
			strcpy(buff, token);
			token = strtok(NULL, meta);
			
			i++;
			
		}

		for(i2 = 0; i2 < i; i2++){
			i3 = 0;
			token2 = strtok(argsFull[i2], s);
			if(token2 == NULL){
				argsParsed[i2][0] = argsFull[i2];
			}
			while(token2 != NULL){
				argsParsed[i2][i3] = token2;

				token2 = strtok(NULL, s);
				
				i3++;
			}
			argsParsed[i2][i3] = NULL;
		
		}
		if(strlen(argsParsed[0][0]) != 0){
			process(input2, argsParsed, i);
		}
		
		dup2(stdin_save, 0);
		dup2(stdout_save, 1);
		memset(input, 0, sizeof(input));
	
	}
}
