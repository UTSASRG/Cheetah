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
 * @file   cachetrack.h
 * @brief  Track read/write access on specified cache. 
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */ 


#ifndef _TRACK_H_
#define _TRACK_H_

#include <set>
#include <list>
#include <vector>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <algorithm> //sort
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

#include "atomic.h"

#include "xdefines.h"
#include "ansiwrapper.h"
#include "cacheinfo.h"
#include "cachehistory.h"
#include "spinlock.h"
#include "internalheap.h"

using namespace std;

extern "C" {
  struct wordinfo {
    int         unitsize;
    int         tid;
    int         reads;
    int         writes;
  };
};

class cachetrack {
public:
//  enum { WORD_SIZE = 4 };
  enum { WORD_THREAD_SHARED = 0xFFFFFFFF };
  enum { WORD_THREAD_INIT = xdefines::MAX_THREADS };

  cachetrack(size_t start, unsigned long writes) {
    _cacheStart = start;
    _thisCache.initialize((void *)_cacheStart, CACHE_LINE_SIZE);
    _writes = writes; 
    _accesses = _writes;

    // Set the words information.
    memset(&_words[0], 0, sizeof(struct wordinfo) * xdefines::WORDS_PER_CACHE_LINE); 
    for(int i = 0; i < xdefines::WORDS_PER_CACHE_LINE; i++) {
      _words[i].tid = WORD_THREAD_INIT; 
    }
  }
  
  ~cachetrack (void) {
  }

  // Record word access. We are not holding lock since it is impossible to have race condition.
  // Here, if the access is always double word, we do not care about next word. since 
  // the first word's unit size will be DOUBLE_WORD 
  void recordWordAccess(int tid, void * addr, int bytes, eAccessType type) {
    int index = getCacheOffset((size_t)addr);
    if(tid != _words[index].tid) {
      if(_words[index].tid == WORD_THREAD_INIT) {
        _words[index].tid = tid;
      }
      else if(_words[index].tid != WORD_THREAD_SHARED) {
        _words[index].tid = WORD_THREAD_SHARED;
      }
    }
    _words[index].unitsize = bytes;

    // Update corresponding counter for each word.
    if(type == E_ACCESS_WRITE) {
      _words[index].writes++;
    }
    else {
      _words[index].reads++;
    }
  }

  unsigned long getWrites(void) {
    return _writes;
  }
  
  unsigned long getAccesses(void) {
    return _accesses;
  }
	
	unsigned long getLatency(void) {
		return _latency;
	}

  long getInvalidations(void) {
    long invalidations;
    invalidations = _thisCache.invalidations();
    if(invalidations) {
    //  fprintf(stderr, "CAche invalidations %lx\n", invalidations);
    }
    return invalidations;
  }

  void reportWordAccesses(void) {
    unsigned long i;
    for(i = 0; i < xdefines::WORDS_PER_CACHE_LINE; i++) {
      if(_words[i].writes > xdefines::THRESHOLD_HOT_ACCESSES || _words[i].reads > xdefines::THRESHOLD_HOT_ACCESSES) { 
        fprintf(stderr, "Word %ld: address %lx reads %x writes %x thread %x\n", i, (unsigned long)_cacheStart + i * xdefines::WORD_SIZE, _words[i].reads, _words[i].writes, _words[i].tid);
      }
    } 

  }

  /// Report false sharing
  void reportFalseSharing(void) {
    if(_thisCache.invalidations() > 0) {
      fprintf(stderr, "\tCacheStart 0x%lx Accesses %lx (at %p) Writes %lx invalidations %ld THREAD %d\n", _cacheStart, _accesses, &_accesses,  _writes, _thisCache.invalidations(), getThreadIndex());
    }
//    if(_thisCache.invalidations() < 1) {
    reportWordAccesses();
//    }
  }

  void * getWordinfoAddr(int index) {
    return &_words[index];
  }

  bool hasFalsesharing(void) {
    bool falsesharing = false;
    if(getInvalidations() > 0) { 
      falsesharing = true;
    }

    return falsesharing;
  }

  // Main function to handle each access for a potential cache line (writes larger than 
  // a pre-defined threshold).  
  void handleAccess(pid_t tid, void * addr, int bytes, eAccessType type, unsigned long latency) {
		// Updating the number of accesses and the latency information
		atomic_add(1, (volatile unsigned long *)&_accesses);
		atomic_add(latency, (volatile unsigned long *)&_latency);

		fprintf(stderr, "%p (Thread%d): access %lx latency %lx current latency %lx\n", addr, getThreadIndex(), _accesses, _latency, latency);
 
    // Check whether we need to sample this lines accesses now.
    int wordindex = getCacheOffset((size_t)addr);
   
    // Record the detailed information of this accesses.
    recordWordAccess(tid, addr, bytes, type);

    // Update the writes information 
    if(type == E_ACCESS_WRITE) {
      _writes++;
    }

    // Check whether we will have an invalidation
    _thisCache.handleAccess(tid, (char *)addr, type);
  }

  cacheinfo * getCacheinfo(void) {
    return &_thisCache;
  }

  struct wordinfo * getWordAccesses(void) {
    return &_words[0];
  }

private:
  //inline int getCacheOffset(size_t addr) {
  int getCacheOffset(size_t addr) {
    return ((addr - _cacheStart)/xdefines::WORD_SIZE);
  }
  
  inline unsigned long getAccessAddress(int index) {
    return (_cacheStart + index * xdefines::WORD_SIZE);
  }

  unsigned long  _accesses;

  // How many writes on this cache line.
  unsigned long  _writes;
	unsigned long  _latency;

  cacheinfo      _thisCache;
  size_t         _cacheStart;

  // Detailed information of all words in current cache line 
  struct wordinfo _words[xdefines::WORDS_PER_CACHE_LINE];
};

#endif

