#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdarg.h>
#include "cachetrack.h"
#include "xrun.h"

#ifdef USING_IBS
#include "IBS/ibs.h"
#endif

extern "C" {
  void initializer (void) __attribute__((constructor));
  void finalizer (void)   __attribute__((destructor));
  bool initialized = false;
  unsigned long textStart, textEnd;
  unsigned long globalStart, globalEnd;
  unsigned long heapStart, heapEnd;
	bool _isMultithreading = false;
  #define INITIAL_MALLOC_SIZE 81920
  static char * tempalloced = NULL;
  static int remainning = 0;
  __thread thread_t * current;
  __thread bool isBacktrace = false;
  xmemory  & _memory = xmemory::getInstance();

  // FIXME: this is the function exposed to the hardware performance counter.
  void handleAccess(pid_t tid, unsigned long addr, size_t size, bool isWrite, unsigned long  latency) {
    xmemory::getInstance().handleAccess(tid, addr, size, isWrite, latency);
  }
  
  void initializer (void) {
    // Using globals to provide allocation
    // before initialized.
    // We can not use stack variable here since different process
    // may use this to share information. 
    static char tempbuf[INITIAL_MALLOC_SIZE];

    // temprary allocation
    tempalloced = (char *)tempbuf;
    remainning = INITIAL_MALLOC_SIZE;

    init_real_functions();
 
    xrun::getInstance().initialize();
    initialized = true;
#ifdef USING_IBS
    initIBS();
    startIBS(0); //master thread
#endif
//		fprintf(stderr, "Now we have initialized successfuuly\n"); 
  }

  void finalizer (void) {
    initialized = false;
    xrun::getInstance().finalize();
#ifdef USING_IBS
//    stopIBS(nthreads);
#endif
  }

  // Temporary mallocation before initlization has been finished.
  static void * tempmalloc(int size) {
    void * ptr = NULL;
    if(remainning < size) {
      // complaining. Tried to set to larger
   //   printf("Not enough temporary buffer, size %d remainning %d\n", size, remainning);
      exit(-1);
    }
    else {
      ptr = (void *)tempalloced;
      tempalloced += size;
      remainning -= size;
    }
    return ptr;
  }

  /// Functions related to memory management.
  void * malloc (size_t sz) {
    void * ptr;
  //  printf("**MALLOC sz %ld, initialized %d******\n", sz, initialized);
    if (!initialized) {
      ptr = tempmalloc(sz);
    } else {
   // printf("**actual MALLOC sz %ld *\n", sz);
      ptr = xmemory::getInstance().malloc (sz);
    }
    if (ptr == NULL) {
      fprintf (stderr, "Out of memory!\n");
      ::abort();
    }
//    fprintf(stderr, "********MALLOC sz %ld ptr %p***********\n", sz, ptr);
    return ptr;
  }
  
  void * calloc (size_t nmemb, size_t sz) {
    void * ptr;
    
    ptr = malloc (nmemb * sz);
	  memset(ptr, 0, sz*nmemb);
    return ptr;
  }

  void free (void * ptr) {
    // We donot free any object if it is before 
    // initializaton has been finished to simplify
    // the logic of tempmalloc
    if(initialized) {
      xmemory::getInstance().free (ptr);
    }
  }

  size_t malloc_usable_size(void * ptr) {
    if(initialized) {
      return xmemory::getInstance().malloc_usable_size(ptr);
    }
    return 0;
  }

  // Implement the initialization
  void * memalign (size_t boundary, size_t size) {
    if(initialized) {
      return xmemory::getInstance().memalign(boundary, size);
    }
	  fprintf(stderr, "Does not support memalign. boundary %lx, size %lx\n", boundary, size);
//    ::abort();
    return NULL;
  }

  void * realloc (void * ptr, size_t sz) {
    return xmemory::getInstance().realloc (ptr, sz);
  }
  
	// Intercept the pthread_create function.
  int pthread_create (pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine) (void *), void * arg)
  {
     return xthread::getInstance().thread_create(tid, attr, start_routine, arg);
  }

	// Intercept the pthread_join function. Thus, 
	// we are able to know that how many threads have exited.
	int pthread_join(pthread_t thread, void **retval) {
     return xthread::getInstance().thread_join(thread, retval);
	}
};

