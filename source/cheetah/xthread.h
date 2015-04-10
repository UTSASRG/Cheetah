
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
#include "threadmap.h"

#ifdef USING_IBS
#include "IBS/ibs.h"
#endif

class xthread {
private:
    xthread() 
    { }

public:
	struct threadLevelInfo {
		int beginIndex;
		int endIndex;
		struct timeinfo startTime;
		unsigned long elapse;
	};

  static xthread& getInstance() {
    static char buf[sizeof(xthread)];
    static xthread * theOneTrueObject = new (buf) xthread();
    return *theOneTrueObject;
  }

  /// @brief Initialize the system.
  void initialize()
  {
    _aliveThreads = 0;
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
    pthread_mutex_init(&_lock, NULL);

    // Shared the threads information. 
    memset(&_threads, 0, sizeof(_threads));

		// Set all entries to be available initially 
    for(int i = 0; i < xdefines::NUM_HEAPS; i++) {
			_HeapAvailable[i] = true;
    }

		// Set this thread level information to 0.
		memset(&_threadLevelInfo, 0, sizeof(struct threadLevelInfo)*xdefines::MAX_THREAD_LEVELS);

		// Initialize the threadmap.
		threadmap::getInstance().initialize();

    // Allocate the threadindex for current thread.
    initInitialThread();
  }

	void insertThreadMap(pthread_t tid, thread_t * thread) {
		threadmap::getInstance().insertThread(tid, thread);
	}

	void removeThreadMap(pthread_t tid) {
		threadmap::getInstance().removeThread(tid);
	}

	void removeThreadMap(thread_t * thread) {
		removeThreadMap(thread->self);
	}

	thread_t * getThreadInfoByTid(pthread_t tid) {
		return threadmap::getInstance().getThreadInfo(tid);
	}

  // Initialize the first threadd
  void initInitialThread(void) {
    int tindex;
    
     // Allocate a global thread index for current thread.
    tindex = allocThreadIndex( );

    // Get corresponding thread_t structure.
    current  = getThreadInfoByIndex(tindex);
    current->self  = pthread_self();
		insertThreadMap(current->self, current);
  }

  thread_t * getThreadInfoByIndex(int index) {
    return &_threads[index];
  }

	// Updating the end index in every allocThreadIndex
	// There is no need to hold the lock since only the main thread can call allocThreadIndex
	void updateThreadLevelInfo(int threadindex) {
		struct threadLevelInfo * info = &_threadLevelInfo[_threadLevel];

		if(info->endIndex < threadindex) {
			info->endIndex = threadindex;
		}
	}

	// Start a thread level
	void startThreadLevelInfo(int threadIndex) {
		struct threadLevelInfo * info = &_threadLevelInfo[_threadLevel];

		start(&info->startTime);
		info->beginIndex = threadIndex;
		info->endIndex = threadIndex;
	}
	
	// Start a thread level
	void stopThreadLevelInfo(void) {
		struct threadLevelInfo * info = &_threadLevelInfo[_threadLevel];

		info->elapse = elapsed2ms(stop(&info->startTime, NULL));

		fprintf(stderr, "PHASE end %ld\n", info->elapse);
	}

  // Allocate a thread index under the protection of global lock
  int allocThreadIndex(void) {
		global_lock();
		int index = _threadIndex++;
		int alivethreads = _aliveThreads++;

		fprintf(stderr, "allocThreadIndex in the beginning, with index %d alivethread %d\n", index, alivethreads);
		// Check whether we have created too many threads or there are too many alive threads now.
    if(index >= xdefines::MAX_THREADS || alivethreads >= xdefines::MAX_ALIVE_THREADS) {
      fprintf(stderr, "Set xdefines::MAX_THREADS to larger. _alivethreads %ld totalthreads %ld maximum alive threads %d", _aliveThreads, _threadIndex, xdefines::MAX_ALIVE_THREADS);
      abort(); 
    } 

		// Initialize 
    thread_t * thread = getThreadInfoByIndex(index);
		thread->ptid = gettid();
		thread->index = index;
		thread->latency = 0;
		thread->accesses = 0;
		start(&thread->startTime);

		fprintf(stderr, "allocThreadIndex line %d\n", __LINE__);

		// Now find one available heapid for this thread.
		thread->heapid = allocHeapId();

		// Check whether we are still in the the normal case
		if(thread->ptid != _origThreadId) {
		//	fprintf(stderr, "thread->ptid %d origthreadid %d\n", thread->ptid, _origThreadId);
			_predPerfImprovement = false;
		}
 
		if(_predPerfImprovement != true) {
			global_unlock();
		//	fprintf(stderr, "RETURN INVALID index %d\n", index);
			return index;
		}

		// If alivethreads is 1, we are creating new threads now.
		fprintf(stderr, "allocThreadIndex line %d\n", __LINE__);
		if(alivethreads == 0) {
			// We need to save the starting time
			startThreadLevelInfo(index);
		}
		else if(alivethreads == 1) {
			// Now we are trying to create more threads
			// Serial phase is ended now.
			_isMultithreading = true;
		
			// Now we get the elapse of the serial phase	
			stopThreadLevelInfo();
			
			// Now we are entering into a new level
			_threadLevel++;		
			startThreadLevelInfo(index);
		}
		else if (alivethreads > 1) {
			// We don't know how many threads are we going to create.
			// thus, we simply update the endindex now.
			updateThreadLevelInfo(index);
		}

		//fprintf(stderr, "threadindex %d\n", _threadIndex);
		if(alivethreads == 0) {
			// Set the pindex to 0 for the initial thread
			thread->pindex = 0;
			thread->parentRuntime = 0;
		}
		else {
			thread->parentRuntime=getParentRuntime(thread->pindex);
			thread->pindex = getThreadIndex();
		}

		global_unlock();
    
    return index; 
  }

	// How we can get the parent's runtime on the last epoch?
	unsigned long getParentRuntime(int index) {
		thread_t *thread = getThreadInfoByIndex(index);

		return thread->actualRuntime;
	}

  /// Create the wrapper 
  /// @ Intercepting the thread_creation operation.
  int thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
    void * ptr = NULL;
    int tindex;
    int result;

    // Allocate a global thread index for current thread.
    tindex = allocThreadIndex();
    thread_t * children = getThreadInfoByIndex(tindex);
    
    children->startRoutine = fn;
    children->startArg = arg;

    result =  WRAP(pthread_create)(tid, attr, startThread, (void *)children);

    return result;
  }      


	int thread_join(pthread_t thread, void **retval)  {
		int ret;
		thread_t * thisThread;

		
		ret = WRAP(pthread_join(thread, retval));

		if(ret == 0) {
			// Finding out this thread.
			thisThread = getThreadInfoByTid(thread);
			markThreadExit(thisThread);
		}

		return ret;
	}

  // @Global entry of all entry function.
  static void * startThread(void * arg) {
    void * result;

    current = (thread_t *)arg;

#ifdef USING_IBS
    startIBS(current->index);
#endif
    current->self = pthread_self();

    // from the TLS storage.
    result = current->startRoutine(current->startArg);

		// Get the stop time.
		current->actualRuntime = elapsed2ms(stop(&current->startTime, NULL));
 
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
      thread = getThreadInfoByIndex(index);
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
      thread = getThreadInfoByIndex(index);
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


private:
  /// @brief Lock the lock.
  void global_lock(void) {
    pthread_mutex_lock(&_lock);
  }

  /// @brief Unlock the lock.
  void global_unlock(void) {
    pthread_mutex_unlock(&_lock);
  }

	/// @ Allocation should be always successful.
	int allocHeapId(void) {
		int heapid;

		while(true) {
			if(_HeapAvailable[_heapid] == true) {
				heapid = _heapid;
				_HeapAvailable[_heapid] = false;
				_heapid = (_heapid+1)%xdefines::NUM_HEAPS;
				break;	
			}
			_heapid = (_heapid+1)%xdefines::NUM_HEAPS;
		}
	
		return heapid;
	}

	// Set the heap to be available if the thread is exiting.
	void releaseHeap(int heapid) {
		_HeapAvailable[heapid] = true;
	}
 
	// Now we will mark the exit of a thread 
  void markThreadExit(thread_t * thread) {
  //  fprintf(stderr, "remove thread %p with thread index %d\n", thread, thread->index);
    global_lock();

    --_aliveThreads;

		if(_aliveThreads == 1) {
			_isMultithreading = false;
			
			// Now we have to update latency information for the current level
    	if(_predPerfImprovement) {
				stopThreadLevelInfo();

				// Now we will udpate the level.
				_threadLevel++;

				// Now we will start a new serial phase.			
				startThreadLevelInfo(_threadIndex);
    	}
		}

		// Release the heap id for this thread.
		releaseHeap(thread->heapid);

    global_unlock();
  }


  pthread_mutex_t _lock;
  volatile unsigned long _threadIndex;
  volatile unsigned long _aliveThreads;
  int _tid;
	int _numCPUs;
	pid_t _origThreadId;
	int _heapid;
		
	// We are adding a total latency here.
	bool  _predPerfImprovement;

	bool     _HeapAvailable[xdefines::NUM_HEAPS];
  // Total threads we can support is MAX_THREADS
  thread_t  _threads[xdefines::MAX_THREADS];

	// We will update these information
	int _threadLevel;
	struct threadLevelInfo _threadLevelInfo[xdefines::MAX_THREAD_LEVELS];
};
#endif

