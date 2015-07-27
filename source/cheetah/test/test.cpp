#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <iterator>
#include <string>
#include <ctime>

#define NUM_THREADS 8

#define MAX_COUNT 1000000

unsigned long array[8];

//extern "C" void handleAccess(pid_t tid, unsigned long addr, size_t size, bool isWrite);

void *thr_func(void *arg) {
	unsigned long index = (unsigned long)arg;

	fprintf(stderr, "At thread %ld\n", index);

	for (int j = 0; j < 100; j++)
	for(int i = 0; i < MAX_COUNT; i++) {
		array[index]++;
//		handleAccess(index, (unsigned long)&array[index], 8, true);
	}
}


int main(int argc, char *argv[])
{ 
		int rc;
		pthread_t tid[NUM_THREADS];
		unsigned long i;

	
		for(i = 0; i < 8; i++) {
			array[i] = 0;
		}

		fprintf(stderr, "Main function after initializing the global array\n");
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
