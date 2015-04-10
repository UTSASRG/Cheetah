// -*- C++ -*-

/*
 Copyright (c) 2007-2013 , University of Massachusetts Amherst.

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
 * @file   threadmap.h
 * @brief  Mapping between pthread_t and internal thread index.
 *         We also keep the thread running time on each entry.
 *         For different thread entry, we can also keep track of the address of this mapping.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#ifndef _THREADMAP_H_
#define _THREADMAP_H_

#include <sys/types.h>
#include <syscall.h>
#include <sys/syscall.h>
#include "xdefines.h"
#include "hashmap.h"
#include "hashfuncs.h"
#include "spinlock.h"

class threadmap {

public:
  threadmap()
  {
  }
 
  static threadmap& getInstance() {
    static char buf[sizeof(threadmap)];
    static threadmap * theOneTrueObject = new (buf) threadmap();
    return *theOneTrueObject;
  }
 
  void initialize() {
    _xmap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, xdefines::MAX_THREADS);
  } 

  thread_t * getThreadInfo(pthread_t tid) {
    thread_t * info = NULL;
    _xmap.find((void *)tid, sizeof(void *), &info);
    return info;
  } 
 
  void deleteThreadMap(pthread_t tid) {
    _xmap.erase((void *)tid, sizeof(void*));
  }

  void insertThread(pthread_t tid, thread_t * thread) {
    // Malloc 
    _xmap.insert((void *)tid, sizeof(void *), thread);
  }

  void removeThread(pthread_t tid) {
    // First, remove thread from the threadmap.
    deleteThreadMap(tid);
  }

private:
  // We are maintainning a private hash map for each thread.
  typedef HashMap<void *, thread_t *, spinlock, InternalHeapAllocator> threadHashMap;

  // The  variables map shared by all threads
  threadHashMap _xmap;
};

#endif
