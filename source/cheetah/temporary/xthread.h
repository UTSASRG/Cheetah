
// -*- C++ -*-

/*
Allocate and manage thread index.
Second, try to maintain a thread local variable to save some thread local information.
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
 * @file   xthread.h
 * @brief  Managing the thread creation, etc.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#ifndef _XTHREAD_H_
#define _XTHREAD_H_

#include <new>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "xdefines.h"
#include "finetime.h"
#ifdef USING_IBS
#include "IBS/ibs.h"
#endif

class xthread {
private:
    xthread() 
    { }

public:

  static xthread& getInstance() {
    static char buf[sizeof(xthread)];
    static xthread * theOneTrueObject = new (buf) xthread();
    return *theOneTrueObject;
  }

  /// @brief Initialize the system.
  void initialize()
  {
		_totalAccesses = 0;
		_totalLatency = 0;
    _aliveThreads = 0;
		_threadLevel = 0;
		_threadIndex = 0;
    _heapid = 0;
		_predPerfImprovement = true;
		_origThreadId = gettid();

		// Acquire the number of CPUs.
		_numCPUs= sysconf(_SC_NPROCESSORS_ONLN);
		if(_numCPUs < 0) {
			fprintf(stderr, "Can't get the CPU's number\n");
			abort();
		}

		// How we can get the information in the beginning.
    _totalThreads = xdefines::MAX_THREADS;
  
    pthread_mutex_init(&_lock, NULL);

    // Shared the threads information. 
    memset(&_threads, 0, sizeof(_threads));

    // Initialize all mutex.
    thread_t * thisThread;
 
		// Set all entries to be available initially 
    for(int i = 0; i < xdefines::NUM_HEAPS; i++) {
			_HeapAvailable[i] = true;
    }

    // Allocate the threadindex for current thread.
    initInitialThread();
		
	//	start(&origtime); 
  }

  // Initialize the first threadd
  void initInitialThread(void) {
    int tindex;
    
     // Allocate a global thread index for current thread.
    tindex = allocThreadIndex();

    // First, xdefines::MAX_ALIVE_THREADS is too small.
    if(tindex == -1) {
      PRDBG("The alive threads is larger than xefines::MAX_THREADS larger!!\n");
      assert(0);
    }

    // Get corresponding thread_t structure.
    current  = getThreadInfo(tindex);
    current->self  = pthread_self();
  }


  thread_t * getThreadInfo(int index) {
    assert(index < _totalThreads);

    return &_threads[index];
  }

  // Allocate a thread index under the protection of global lock
  int allocThreadIndex(void) {
		int index = atomic_increment_and_return(&_threadIndex);

    if(index >= xdefines::MAX_THREADS || _aliveThreads >= xdefines::MAX_ALIVE_THREADS) {
      fprintf(stderr, "Set xdefines::MAX_THREADS to larger. _alivethreads %d totalthreads %d maximum alive threads %d", _aliveThreads, _totalThreads, xdefines::MAX_ALIVE_THREADS);
      abort(); 
    } 

		// If child threads are creating new threads,
		// it is too complicated to predict performance improvement after fixes.
		if(getTid() != _origThreadId) {
			_predPerfImprovement = false;
		}

		// Now getting the tid of the parent since 
		// parent will call allocThreadIndex();
		//fprintf(stderr, "threadindex %d\n", _threadIndex);
    thread_t * thread = getThreadInfo(index);

		// We change the tid here so that we don't have to set it elsewhere.
		thread->ptid = getTid();
		thread->pindex = getThreadIndex();
		thread->index = index;
		thread->latency = 0;
		thread->accesses = 0;

		// Now find one available heapid for this thread.
		thread->heapid = allocHeapId();

    // A thread is counted as alive when its structure is allocated.
    _aliveThreads++;


    _threadIndex = _threadIndex+1;
    
    return index; 
  }

  /// Create the wrapper 
  /// @ Intercepting the thread_creation operation.
  int thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
    void * ptr = NULL;
    int tindex;
    int result;

    // Lock and record
    global_lock();

    // Allocate a global thread index for current thread.
    tindex = allocThreadIndex();
    
    // First, xdefines::MAX_ALIVE_THREADS is too small.
    if(tindex == -1) {
      PRDBG("The alive threads is larger than xefines::MAX_THREADS larger!!\n");
      assert(0);
    }
 
    // Get corresponding thread_t structure.
    thread_t * children = getThreadInfo(tindex);
    
    children->startRoutine = fn;
    children->startArg = arg;

    global_unlock();

    result =  WRAP(pthread_create)(tid, attr, startThread, (void *)children);

		//fprintf(stderr, "after the thread_create, tid %d index %d\n", *tid, tindex);
    // Set up the thread index in the local thread area.
    return result;
  }      


  // @Global entry of all entry function.
  static void * startThread(void * arg) {
    void * result;

    current = (thread_t *)arg;

		//fprintf(stderr, "THREAD_CREATE arg %p!!!!!\n", current);
#ifdef USING_IBS
    startIBS(current->index);
#endif
		start(&current->startTime);
    current->self = pthread_self();

    // from the TLS storage.
    result = current->startRoutine(current->startArg);

		// Get the stop time.
		current->actualRuntime = elapsed2ms(stop(&current->startTime, NULL));
 
    // Decrease the alive threads
    xthread::getInstance().removeThread(current);
    
    return result;
  }
	
	// In the end, we should compute the total latency.
	unsigned long getTotalLatency(int startIndex, int stopIndex) {
    int index = startIndex;

		unsigned long latency = 0;

		//fprintf(stderr, "threadindex %d\n", _threadIndex);
    thread_t * thread;
    while(true) {

			// Get the latency of all active threads. 
      thread = getThreadInfo(index);
			latency += thread->latency;				        

			index++;
			
			// We will skipp the original thread.
			if(index%xdefines::MAX_THREADS == 0) {
				index = 1; 
			}
			
			// If we have checked all threads, let's return now.
			if(index == stopIndex) {
				break;
			}
		}

		// Return the full latency
		return latency;
	}

	// In the end, we should compute the total latency.
	unsigned long getTotalAccesses(int startIndex, int stopIndex) {
    int index = startIndex;

		unsigned long accesses = 0;

		//fprintf(stderr, "threadindex %d\n", _threadIndex);
    thread_t * thread;
    while(true) {

			// Get the latency of all active threads. 
      thread = getThreadInfo(index);
			accesses += thread->accesses;				        

			index++;
			
			// We will skipp the original thread.
			if(index%xdefines::MAX_THREADS == 0) {
				index = 1; 
			}
			
			// If we have checked all threads, let's return now.
			if(index == stopIndex) {
				break;
			}
		}
		
		return accesses;

	}
	// In the end, we should compute the total latency.
	unsigned long getTotalLatency(void) {
    int index = 0;

		unsigned long latency = _totalLatency;

		//fprintf(stderr, "threadindex %d\n", _threadIndex);
    thread_t * thread;
    while(index < xdefines::MAX_THREADS) {
			// Get the latency of all active threads. 
      thread = getThreadInfo(index);
      if(!thread->available) {
				latency += thread->latency;				        
			}

			index++;
		}

		// Return the full latency
		return latency;
	}

	// In the end, we should compute the total latency.
	unsigned long getTotalAccesses(void) {
    int index = 0;

		unsigned long accesses = _totalAccesses;

		//fprintf(stderr, "threadindex %d\n", _threadIndex);
    thread_t * thread;
    while(index < xdefines::MAX_THREADS) {
			// Get the latency of all active threads. 
      thread = getThreadInfo(index);

			//fprintf(stderr, "thread->available %d\n", thread->available);
			//fprintf(stderr, "thread->accesses %ld\n", thread->accesses);

      if(!thread->available) {
				accesses += thread->accesses;				        
			}

			index++;
		}

		printf("in the end of getTotal Accesses %lx\n", accesses);
		// Return the full latency
		return accesses;
	}


private:
  /// @brief Lock the lock.
  void global_lock(void) {
    pthread_mutex_lock(&_lock);
  }

  /// @brief Unlock the lock.
  void global_unlock(void) {
    pthread_mutex_unlock(&_lock);
  }

	int allocHeapId(void) {
    global_lock();

    global_unlock();
	}
  
  void removeThread(thread_t * thread) {
  //  fprintf(stderr, "remove thread %p with thread index %d\n", thread, thread->index);
    global_lock();

		// Now we update the total latency of the system.
		_totalLatency += current->latency;
		_totalAccesses += current->accesses;

    current->available = true;
    --_aliveThreads;

		// Now we have to update latency information for the current level
    if(_aliveThreads == 1 && _predPerfImprovement) {
			// We will save the latency information for the last level.   
			int threads;

			// Now we will udpate the level.
			_threadLevel++;
    }

    global_unlock();
  }


  pthread_mutex_t _lock;
  volatile unsigned long _threadIndex;
  int _tid;
  int _aliveThreads;
	int _numCPUs;
	pid_t _origThreadId;
	int _heapid;
		
	// We are adding a total latency here.
	unsigned long _totalAccesses; 
	unsigned long _totalLatency;  

	bool  _predPerfImprovement;

	bool     _HeapAvailable[xdefines::NUM_HEAPS];
  // Total threads we can support is MAX_THREADS
  thread_t  _threads[xdefines::MAX_THREADS];

	// We will update these information
	int _threadLevel;
//	latencyInfo _latency[xdefines::MAX_THREAD_LEVELS];
};
#endif

