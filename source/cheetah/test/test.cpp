#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <iterator>
#include <string>
#include <ctime>

#define NUM_THREADS 8

#define MAX_COUNT 1000000
//#define MAX_COUNT 1000
//#define MAX_COUNT 100

unsigned long array[8];
unsigned long nonshared_array[8];

extern "C" void handleAccess(pid_t tid, unsigned long addr, size_t size, bool isWrite, unsigned long latency);

void *thr_func(void *arg) {
	unsigned long index = (unsigned long)arg;

//	for (int j = 0; j < 100; j++)
	for(int i = 0; i < MAX_COUNT; i++) {
		array[index]++;

	#ifndef USING_IBS
	//	handleAccess(index, (unsigned long)&array[index], 8, true, 2);
	#endif
	}
}


int main(int argc, char *argv[])
{ 
		int rc;
		pthread_t tid[NUM_THREADS];
		unsigned long i;
		int index = 0;
	
		for(i = 0; i < 8; i++) {
			array[i] = 0;
			nonshared_array[i] = 0;
		}

		// Try to issue non false sharing writes
	#ifndef USING_IBS
		for(int i = 0; i < MAX_COUNT; i++) {
			index++;
			if(index == 8) {
				index = 0;
			}
			nonshared_array[index]++;
			//handleAccess(index, (unsigned long)&nonshared_array[index], 8, true);
//			fprintf(stderr, "main function after initialization\n");
			handleAccess(0, (unsigned long)&nonshared_array[index], 8, true, 2);
		}
	#endif

//	exit(0);
	//	fprintf(stderr, "Main function, array address %p nonshared_array %p\n", array, nonshared_array);
  //	exit(0);
		// Creating threads
    for (i=0;i<NUM_THREADS ;i++ )
    {
     		fprintf(stderr, "USER: create thread %ld\n", i);
   			if (rc = pthread_create(&tid[i], NULL, thr_func, (void *)i))
        {
          fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
          return EXIT_FAILURE;
        }
			  fprintf(stderr, "USER: Main function, creating thread %ld\n", i);
    }


    for (i = 0; i < NUM_THREADS; ++i) {
        pthread_join(tid[i], NULL);
    }


    printf("Done\n");


  return EXIT_SUCCESS;
}
