#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>

extern "C" {
	#define HARDWARE_CORES_NUM 16
	unsigned long threadIndex = 0;

  typedef void * threadFunction (void *);

	typedef struct {
		threadFunction * startRoutine;
  	void * startArg;
		int    threadIndex;
	} startStruct; 
		

	startStruct threads[4096];

	void initializer (void) __attribute__((constructor));

	// Setting up the first thread.
	void initializer (void) {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);

      CPU_SET(threadIndex++%HARDWARE_CORES_NUM, &cpuset);

      pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	}

	void * startThread(void * arg) {
  	startStruct * mythread = (startStruct *)arg;;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

		//fprintf(stderr, "mythread->index %d\n", mythread->threadIndex);
 //   CPU_SET((mythread->threadIndex%HARDWARE_CORES_NUM)*2, &cpuset);
    CPU_SET(mythread->threadIndex%HARDWARE_CORES_NUM, &cpuset);

    // Seting up the affinity
    int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

		//fprintf(stderr, "Thread %d the core %d result %d\n", mythread->threadIndex, (mythread->threadIndex%HARDWARE_CORES_NUM)*2, result);
		mythread->startRoutine(mythread->startArg);

		//fprintf(stderr, "finish thread %d\n", mythread->threadIndex);	
		return NULL;	
	}
	  
	// Intercept the pthread_create function.
  int pthread_create (pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine) (void *), void * arg)
  {
  	typedef int (*real_pthread_create) (pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine) (void *), void * arg);

    static real_pthread_create thread_create  = (real_pthread_create) dlsym(RTLD_NEXT, "pthread_create");
		int index = __atomic_fetch_add(&threadIndex, 1, __ATOMIC_RELAXED);
	
		threads[index].startRoutine = start_routine;
		threads[index].startArg = arg;
		threads[index].threadIndex = index;

		return thread_create(tid, attr, startThread, (void *)&threads[index]);
  }
};

