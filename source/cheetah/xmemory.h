// -*- C++ -*-

/*
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
 * @file   xmemory.h
 * @brief  Memory management for all.
 *         This file only includes a simplified logic to detect false sharing problems.
 *         It is slower but more effective.
 * 
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */ 

#ifndef _XMEMORY_H
#define _XMEMORY_H

//#include <signal.h>

#include <sys/types.h>
#include <unistd.h>
#include <set>
#include <sys/types.h>

#include "xglobals.h"
#include "xpheap.h"
#include "xoneheap.h"
#include "xheap.h"
#include "callsite.h"
#include "objectheader.h"
#include "finetime.h"
#include "atomic.h"

#include "xdefines.h"
#include "ansiwrapper.h"
#include "cachetrack.h"
#include "internalheap.h"
#include "spinlock.h"
#include "report.h"
#include "threadmap.h"
#include "mm.h"

class xmemory {
private:

  // Private on purpose. See getInstance(), below.
  xmemory()
  {
  }

  #define CALLSITE_SIZE sizeof(CallSite)

public:

  // Just one accessor.  Why? We don't want more than one (singleton)
  // and we want access to it neatly encapsulated here, for use by the
  // signal handler.
  static xmemory& getInstance() {
    static char buf[sizeof(xmemory)];
    static xmemory * theOneTrueObject = new (buf) xmemory();
    return *theOneTrueObject;
  }

  void initialize() {
  	// Initialize the heap and globals. Basically, we need 
		// spaces to hold access data for both heap and globals.
	  _heap.initialize(USER_HEAP_SIZE);
    _globals.initialize();   

	//	fprintf(stderr, "Initialize the heap and globals\n");   	
// Initialize the status word of cache line. 
    initCachelineStatus();
    _heapCacheIndex = getCachelineIndex(USER_HEAP_BASE);
	//	fprintf(stderr, "Initialize the heap and globals and cacheline status\n");   	
  }

  void finalize(void) {
    reportFalseSharing();
  }
 
  // Report false sharing for current mapping
  void reportFalseSharing(void) {
    // Initialize the reporter in the end.
    report reporter;

    // Reporting the false sharing in the globals.
//    unsigned long index = getCachelineIndex(globalStart);
    reporter.initialize(false, (void *)globalStart, (void *)globalEnd, (void *)_cacheWrites, _cacheTrackings);
		
    reporter.reportFalseSharing();

    void * heapEnd = _heap.getHeapPosition();
    reporter.initialize(true, (void *)USER_HEAP_BASE, heapEnd, (void *)_cacheWrites, _cacheTrackings);
    reporter.reportFalseSharing();
  }


  inline void storeCallsite(unsigned long start, CallSite * callsite) {
    memcpy((void *)(start - CALLSITE_SIZE), (void *)callsite, CALLSITE_SIZE);
  }

  inline void *malloc (size_t sz) {
    void * ptr = NULL;
		
		if(ptr == NULL) {
//  	  printf("xmemory::malloc sz %lx ptr %p\n", sz, ptr);
    	ptr = _heap.malloc(getHeapId(), sz);
  	}

    // Whether current thread is inside backtrace phase
    // Get callsite information.
    CallSite callsite;
    callsite.fetch();
    //callsite.print();

    storeCallsite((unsigned long)ptr, &callsite);
//  	printf("in the end, xmemory::malloc sz %lx ptr %p\n", sz, ptr);
    
    return (ptr);
  }

  inline void * realloc (void * ptr, size_t sz) {
    size_t s = getSize (ptr);

    void * newptr = malloc(sz);
    if(newptr && s != 0) {
      size_t copySz = (s < sz) ? s : sz;
      memcpy (newptr, ptr, copySz);
    }

    free(ptr);
    return newptr;
  }

  inline void * memalign(size_t boundary, size_t size) {
    void * ptr;
    unsigned long wordSize = sizeof(unsigned long);

    if(boundary < 8 || ((boundary & (wordSize-1)) != 0)) {
      fprintf(stderr, "memalign boundary should be larger than 8 and aligned to at least 8 bytes. Acutally %ld!!!\n", boundary);
      abort();
    }

    ptr = malloc(boundary + size);

    // Finding the word aligned with boundary.
    unsigned long start = (unsigned long)ptr + wordSize;
    unsigned long end = (unsigned long)ptr + boundary;

    while(start <= end) {
      if((start & (boundary-1)) == 0) {
        unsigned long * prev = (unsigned long *)(start - wordSize);
        *prev = MEMALIGN_MAGIC_WORD;
        ptr = (void *)start;
        break;
      }

      start += wordSize;
    }
   
    return ptr; 
  }

  inline bool isMemalignPtr(void * ptr) {
    return (*((unsigned long *)((intptr_t)ptr - sizeof(unsigned long))) == MEMALIGN_MAGIC_WORD) ? true : false;
  }

  // We only check 
  inline void * getRealStart(void * ptr) {
    unsigned long objectHeaderSize = sizeof(objectHeader);
    unsigned long * start = (unsigned long *)((intptr_t)ptr - sizeof(unsigned long)); 
    unsigned long * end = (unsigned long *)((intptr_t)ptr - xdefines::PageSize);
    void * realptr = NULL;
    objectHeader * object = NULL;

    while(start > end) {
      object = (objectHeader *)((intptr_t)start - objectHeaderSize);
      if(object->isValidObject()) {
        realptr = start; 
        break;
      }
 
      start--;
    } 
    
    return realptr;
  }

  // When a memory object is freed, this object can be used as a false sharing object. Then we may cleanup 
  // all information about the whole cache line if there is no false sharing there.
  // FIXME: if part of cache line has false sharing problem, then we may not utilize this 
  // cache line but other parts can be reused??  
  inline void free (void * ptr) {
    if(ptr == NULL) 
      return;

    void * realPtr = ptr;
    if(isMemalignPtr(ptr)) {
      realPtr = getRealStart(ptr);
    }

    size_t s = getSize(realPtr);
    // Actually, we should check whether we need to free an object here.
    // If it includes the false sharing suspect, we should check that.
    if(!hasFSSuspect(ptr, s)) {
      _heap.free(getHeapId(), realPtr);
    }
  }

  size_t malloc_usable_size(void * ptr) {
    return getSize(ptr);
  }
  
  /// @return the allocated size of a dynamically-allocated object.
  inline size_t getSize (void * ptr) {
    // Just pass the pointer along to the heap.
    return _heap.getSize (ptr);
  }
 
  inline size_t calcCachelines(size_t size) {
    return size >> CACHE_LINE_SIZE_SHIFTS;
  }
  
  inline unsigned long getCachelineIndex(unsigned long addr) {
    return (addr >> CACHE_LINE_SIZE_SHIFTS);
  }


  /* Initialize the cache line status.
     Currently, the memory less than MAX_USER_BASE 
     will have a corresponding status and tracking status.
     This basic idea is borrow from the AddressSanitizer so that 
     we do not need to know the details of global range.
  */
  void initCachelineStatus(void) {
    size_t cachelines; // How many number of cache lines for this map
    size_t cacheStatusSize; // How much memory  of cache line status

    // We are mapping all memory under the shadow status.
    size_t size = MAX_USER_SPACE;

    // Calculate cache lines information.
    cachelines = calcCachelines(size);

    // Allocate a mapping to hold cache line status.
    size_t sizeStatus = cachelines * sizeof(unsigned long);  

    _cacheWrites = (unsigned long *)MM::mmapAllocatePrivate(sizeStatus, (void *)CACHE_STATUS_BASE); 
    _cacheTrackings = (cachetrack **)MM::mmapAllocatePrivate(sizeStatus, (void*)CACHE_TRACKING_BASE);

    assert(sizeof(unsigned long) == sizeof(cachetrack *));
  }
  
  inline unsigned long * getCacheWrites(unsigned long addr) {
    size_t index = getCachelineIndex(addr);
    return &_cacheWrites[index]; 
  }

  // Whether the specified cache line has false sharing suspect inside?
  // FIXME: currently, we are using simple mechanism when the cache line
  // has writes more than one specified threshold.
  // In the future, we should definitely check the detailed status.
  inline bool hasFSSuspect(unsigned long * status) {
    return (*status > xdefines::THRESHOLD_TRACK_DETAILS);
  }
  
  // Checking whether a object has false sharing suspects inside.
  // Currently, we only check the cacheWrites. If this object includes 
  // cache line with instensive writing, simply return yes.
  inline bool hasFSSuspect(void * addr, size_t size) {
    bool hasSuspect = false; 
    size_t lines = size/CACHE_LINE_SIZE;

    if(size % CACHE_LINE_SIZE) {
      lines++;
    }

    unsigned long * status = getCacheWrites((unsigned long)addr);
   
    for(int i = 0; i < lines; i++) {
      if(hasFSSuspect(&status[i])) {
        hasSuspect = true;
        break;
      }
    }  

    return hasSuspect;
  }

  inline cachetrack * allocCacheTrack(pid_t tid, unsigned long addr, unsigned long totalWrites) {
		// Now we have to get heapid from the tid
		int heapid = getHeapIdByTid(tid);

    void * ptr = InternalHeap::getInstance().malloc(heapid, sizeof(cachetrack));
		//fprintf(stderr, "addr %lx totalWrites %lx ptr %p\n", addr, totalWrites, ptr);
    cachetrack * track = new (ptr) cachetrack(addr, totalWrites);
    return track;
  }

  inline cachetrack * deallocCacheTrack(pid_t tid, cachetrack * track) {
		int heapid = getHeapIdByTid(tid);
		// Now we have to get heapid from the tid
    InternalHeap::getInstance().free(heapid, track);
  }

  // In order to track one cache line, we should make the _cacheWrites larger than the specified threshold.
  inline void enableCachelingTracking(unsigned long index) {
    atomic_exchange(&_cacheWrites[index], xdefines::THRESHOLD_TRACK_DETAILS);
  }

  inline bool isValidObjectHeader(objectHeader * object, unsigned long * address) {
    unsigned long * objectEnd = (unsigned long *)object->getObjectEnd();
    return (object->isValidObject() && address < objectEnd);
  }

  inline unsigned long * getAlignedAddress(unsigned long address) {
    return (unsigned long *)(address & xdefines::ADDRESS_ALIGNED_BITS);
  }

  objectHeader * getHeapObjectHeader(unsigned long * address) {
    objectHeader * object = NULL;
    unsigned long * pos = getAlignedAddress((unsigned long)address);

    // We won't consider that we can not find the magic number because
    // of underflow problem
    while(true) {
      pos--;
      if(*pos == objectHeader::MAGIC) {
        object = (objectHeader *)pos; 
        // Check whether this is a valid object header.
        if(isValidObjectHeader(object, address)) {
          break;
        }
      }
    }

    return object;
  }

  inline unsigned long getCacheStartByIndex(unsigned long index) {
    return index << CACHE_LINE_SIZE_SHIFTS;
  }
  
	unsigned long getCachelineEnd(unsigned long index) {
    return ((index + 1) * CACHE_LINE_SIZE);
  }

  inline cachetrack * getCacheTracking(long index) {
    return (index >= 0 ? (_cacheTrackings[index]) : NULL);
  }

  // Check whether two hot accesses can possibly cause potential false sharing
  bool arePotentialFSHotAccesses(struct wordinfo * word1, struct wordinfo * word2) {
    bool arePotentialFS = false;
    
    // Only one of them has hot writes
    if(word1->writes >= xdefines::THRESHOLD_HOT_ACCESSES 
      || word2->writes >= xdefines::THRESHOLD_HOT_ACCESSES) {
      if(word1->tid != word2->tid) {
        arePotentialFS = true;
      }
      else if(word1->tid == cachetrack::WORD_THREAD_SHARED 
        || word2->tid == cachetrack::WORD_THREAD_SHARED) {
        arePotentialFS = true;
      }
    }

    return arePotentialFS;
  }

  bool checkFalsesharing(unsigned long index) {
    bool falsesharing = false;
    cachetrack * track = getCacheTracking(index);
    if(track) {
      falsesharing = track->hasFalsesharing(); 
    }

    return falsesharing;
  }


  inline void allocateCachetrack(pid_t tid, unsigned long index) {
    size_t cachestart = index << CACHE_LINE_SIZE_SHIFTS;
    cachetrack * track = NULL;
    track = allocCacheTrack(tid, cachestart, xdefines::THRESHOLD_TRACK_DETAILS);
    // Set to corresponding array.
    //fprintf(stderr, "allocCacheTrack index %ld at %p set to %p\n", index, &_cacheTrackings[index], track);
    if(!atomic_compare_and_swap((unsigned long *)&_cacheTrackings[index], 0, (unsigned long)track)) {
      deallocCacheTrack(tid, track);
    }
  }
 
  // Main entry of handle each access
  inline void handleAccess(pid_t tid, unsigned long addr, int bytes, bool isWrite, unsigned long latency) {
    // Xu Liu: Disable the error output from this checking. Hardware sampling check obtain samples from addresses ranges out of the monitored code space.
    if((intptr_t)addr > MAX_USER_SPACE) {
//	 		PRERR("the address seems not right. It should not be larger than %lx\n", MAX_USER_SPACE);
     	return;
    }
	
//		fprintf(stderr, "in xmemory: before updateThreadLatency, latency %ld\n", latency);	
		updateThreadLatency(latency);

	//	fprintf(stderr, "actually check handleAccess at line %d is_Multithreading %d \n", __LINE__, _isMultithreading);
		// We don't actually track the memory access when there is only one thread.
		if(!_isMultithreading) {
			return;
		}

	//	fprintf(stderr, "handleAccess at line %d tid %d\n", __LINE__, tid);

    // We only care those acesses of global and heap.
    unsigned long index = getCachelineIndex(addr);
    cachetrack * track = NULL;
   
		//fprintf(stderr, "addr %lx index %p\n", addr, index); 
    unsigned long * status = &_cacheWrites[index];
    unsigned long totalWrites = *status;
		eAccessType type;

		if(isWrite == true) {
			type = E_ACCESS_WRITE;
		}
		else {
			type = E_ACCESS_READ;
		}

		// If the total writes is less than a threshold, we don't care about 
		// cache line temporary since those cache lines with a big amount of writes can
		// actually cause the performance problem. Thus, currently we only need to update 
		// the number of writes on this cache line.
    if(totalWrites < xdefines::THRESHOLD_TRACK_DETAILS) {
  		//fprintf(stderr, "totalWrites is %ld\n", totalWrites); 
	//	fprintf(stderr, "actually check handleAccess at line %d\n", __LINE__);
	   // Only update writes
      if(type == E_ACCESS_WRITE) {
        if(atomic_increment_and_return(status) == xdefines::THRESHOLD_TRACK_DETAILS-1) {
          size_t cachestart = getCacheStart((void *)addr);
				//	fprintf(stderr, "index is at %ld cachestart %lx\n", index, cachestart);
          track = allocCacheTrack(tid, cachestart, xdefines::THRESHOLD_TRACK_DETAILS);
			//		fprintf(stderr, "index is at %ld, track %p\n", index,  track);

          // Set to corresponding array.
          atomic_compare_and_swap((unsigned long *)&_cacheTrackings[index], 0, (unsigned long)track);
        }
      } // end if type == E_ACCESS_WRITE
    }
    else {
      // Now this cache line is a potential. We update the status.
      track = (cachetrack *)(_cacheTrackings[index]);
      if(track != NULL) {
				// Verify whether this pointer is valid?
        if((intptr_t)track <= xdefines::THRESHOLD_TRACK_DETAILS) {
          PRERR("Not a valid track %p, it should be larger than %x\n", track, xdefines::THRESHOLD_TRACK_DETAILS);
        }
        assert((intptr_t)track > xdefines::THRESHOLD_TRACK_DETAILS);
        track->handleAccess(tid, (void *)addr, bytes, type, latency);
      } 
    }
  }

private:
  objectHeader * getObjectHeader (void * ptr) {
    objectHeader * o = (objectHeader *) ptr;
    return (o - 1);
  }

	thread_t * getThread(pid_t tid) {
		return threadmap::getInstance().getThreadInfo(tid);
	}

	int getHeapIdByTid(pid_t tid) {
		thread_t * thread = getThread(tid);
		
		return thread->heapid;
	}

  /// The protected heap used to satisfy big objects requirement. Less
  /// than 256 bytes now.
  xpheap<xoneheap<xheap> > _heap;
  xglobals   _globals;
  
  unsigned long * _cacheWrites;
  cachetrack ** _cacheTrackings;
  unsigned long _heapCacheIndex; 
};

#endif
