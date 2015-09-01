#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>

extern "C" {
	#define HARDWARE_CORES_NUM 48
	unsigned long threadIndex = 0;

	void initializer (void) __attribute__((constructor));

	// Setting up the first thread.
	void initializer (void) {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);

      CPU_SET(threadIndex++%HARDWARE_CORES_NUM, &cpuset);

      pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	}
	  
	// Intercept the pthread_create function.
  int pthread_create (pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine) (void *), void * arg)
  {
  	typedef int (*real_pthread_create) (pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine) (void *), void * arg);

    static real_pthread_create thread_create  = (real_pthread_create) dlsym(RTLD_NEXT, "pthread_create");
		
		int result = thread_create(tid, attr, start_routine, arg);

		if(result == 0) {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);

      CPU_SET(threadIndex++%HARDWARE_CORES_NUM, &cpuset);

      // Seting up the affinity
      pthread_setaffinity_np(*tid, sizeof(cpu_set_t), &cpuset);
    }
		return result;
  }
};

