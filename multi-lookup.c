/**
* Kate Osborn 
* A multi-threaded application that resolves domain names to IP addresses
*/


#include "util.h"
#include "multi-lookup.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>

#define MAX_INPUT_FILES 10
#define MAX_RESOLVER_THREADS 10
#define MAX_REQUESTER_THREADS 5
#define MAX_NAME_LENGTH 1025
#define MAX_IP_LENGTH INET6_ADDRSTRLEN
#define ARRAY_SIZE 15



// https://stackoverflow.com/questions/7215764/how-to-measure-the-actual-execution-time-of-a-c-program-under-linux 
clock_t start, end;
double cpu_time_used;
char shared_array[ARRAY_SIZE][MAX_NAME_LENGTH];
int count = 0;
int done = 0; 
pthread_cond_t c_requester, c_resolver;
pthread_mutex_t shared_array_lock, resolve_file_lock, request_file_lock, write_file_lock; // protect shared array & access to both resolve & request files 
char* request_file;
char* resolve_file;
char* input_files[MAX_INPUT_FILES];
int file_count; // set in main function
int num_requests;
int num_resolves;


void* request(void* filename){
	int my_count = 0;
	FILE* read_file;
	size_t len= 0;
	char* hostname = malloc(MAX_NAME_LENGTH * sizeof(char));


	while(1) {
		// protect file count with mutex
		pthread_mutex_lock(&request_file_lock);
		if(file_count == 0){
			// if there are no files left to read then release mutex and break out of loop
			pthread_mutex_unlock(&request_file_lock);
			break;
		}
		read_file = fopen(input_files[file_count-1], "r");
		if(!read_file){
			fprintf(stderr, "Could not open file %s, bad input file path.\n", input_files[file_count-1]);
		}
		file_count--;
		printf("file count is now %d\n", file_count);
		// can release the request file mutex that is protecting file count
		pthread_mutex_unlock(&request_file_lock);
 
	
		while(fscanf(read_file, "%1024s", hostname) > 0){
			// grab the lock on shared array, all other threads block here
			pthread_mutex_lock(&shared_array_lock);
			while(count == ARRAY_SIZE)
					pthread_cond_wait(&c_requester, &shared_array_lock); /* if array is full, wait here */
		 	if(count < ARRAY_SIZE){
				strncpy(shared_array[count],hostname, 1025);
				printf("Request thread: %d wrote %s to the array\n", pthread_self(), hostname);
				count++;
				pthread_mutex_unlock(&shared_array_lock); /* release lock on shared-array*/
				pthread_cond_signal(&c_resolver); /*  wake up resolver */
			}

		}
		my_count++;
	
		fclose(read_file);
		
	}
		//set the lock for writing to file
		pthread_mutex_lock(&write_file_lock);
		FILE* write_file = fopen((char*)filename, "a");
		if(!write_file){
			fprintf(stderr, "Failed to open %s file, bad file path.\n", filename);
		}
	 	fprintf(write_file,"Thread %d serviced %d files\n", pthread_self(), my_count);
		fclose(write_file);
		pthread_mutex_unlock(&write_file_lock);
	
	done++;
	free(hostname);

	return NULL;
	

}

void* resolve(void* filename){
	// first lets open the file 
	FILE* fp;
	// will store hostname that we will lookup
	char* hostname;
	// will store address from dnslookup 
	char address[MAX_IP_LENGTH];

	// need to protect count here else race conditions 

	while(1) {
		// short circuit, if all names have been handled then exit
		if(done == num_requests)
			break;
		// grab lock on shared array
		pthread_mutex_lock(&shared_array_lock);
		while(count == 0)
			pthread_cond_wait(&c_resolver, &shared_array_lock); /* wait here until signaled that there is something to resolve*/
		
		if(count > 0 && done < num_requests){
			hostname = shared_array[count-1];
			count--;
			printf("Resolver thread read %s from shared array\n", hostname);
		
			if (dnslookup(hostname, address, sizeof(address)) == UTIL_FAILURE){
				printf("failed DNS lookup %s\n", hostname);
				strncpy(address, " ", sizeof(address)); // 
			}	

		 	pthread_mutex_lock(&resolve_file_lock);
			fp = fopen((char*)filename, "a");
			if(!fp){
				fprintf(stderr, "Failed to open %s file, bad file path.\n", filename);
			}
			fprintf(fp, "%s,%s \n", hostname, address);
			fclose(fp);
			pthread_mutex_unlock(&resolve_file_lock);
			pthread_mutex_unlock(&shared_array_lock);
			pthread_cond_signal(&c_requester); /* wakeup requester thread */
		
		}
		if(done == num_requests && count == 0){
		 	pthread_mutex_unlock(&shared_array_lock);
			break;
		}
			
  
	}

	return NULL;
}


int main(int argc, char* argv[]){
	/* 
	* First argument: num of requester threads 
	* Second argument: num of resolver threads
	* Third argument: name of file in which resolver thread writes IP address
	* Fourth argument: name of file i/w requester threads writes num of files serviced 
	* Rest of the arguments: input files names 
	*/

 

	// do a check to make sure that we have enough arguments
	start = clock();
 if(argc < 6) {
    printf("Not enough arguments..\n");
    return EXIT_FAILURE;
  }
 
	num_requests = atoi(argv[1]);
	num_resolves = atoi(argv[2]);
	resolve_file = argv[3];
 	request_file = argv[4];

	file_count = argc - 5;

		// create list of files 
	for(int i = 5; i < argc; i++){
		input_files[i-5] = argv[i];
	}



	pthread_t requests[num_requests];
	pthread_t resolves[num_resolves];


	// initialize mutexes :https://linux.die.net/man/3/pthread_mutex_init
	// Null just says use default set of attributers 

	pthread_mutex_init( &shared_array_lock, NULL);
	pthread_mutex_init( &resolve_file_lock, NULL);
	pthread_mutex_init( &request_file_lock, NULL);

	// initialize condition vars

	pthread_cond_init( &c_requester, NULL);
	pthread_cond_init( &c_resolver, NULL);

	//create the threads 
	printf("num requests %d \n", num_requests);
	printf("num resolves %d \n", num_resolves);
	printf("file count is %d", file_count);
	
	for(int i = 0; i < num_requests; i++){
		pthread_create(&requests[i], NULL, request, (void*)request_file);

	}

	for(int i = 0; i < num_resolves; i++){
		pthread_create(&resolves[i], NULL, resolve, (void*)resolve_file);
		
	}
	// wait for threads to finish, otherwise main might run to end
	for(int i =0; i< num_requests; i++){
		pthread_join(requests[i] ,NULL);	
	}
	for(int i =0; i< num_resolves; i++){
		pthread_join(resolves[i] ,NULL);	
	}

	// now we can call gettimeofday()

	// cleanup
	pthread_mutex_destroy(&shared_array_lock);
	pthread_mutex_destroy(&request_file_lock);
	pthread_mutex_destroy(&resolve_file_lock);
	pthread_cond_destroy(&c_requester);
	pthread_cond_destroy(&c_resolver);

	end = clock();
	cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
	printf("time elapsed: %f", cpu_time_used);

	return 0;
}
