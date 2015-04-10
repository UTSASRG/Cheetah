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

#ifndef _INTERNALHEAP_H_
#define _INTERNALHEAP_H_

#include "xdefines.h"
#include "sourceinternalheap.h"
#include "xoneheap.h"
#include "xpheap.h"
/**
 * @file InternalHeap.h
 * @brief A shared heap for internal allocation needs.
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 *
 */
class InternalHeap 
{
public:
  InternalHeap()
  {
  }
 
  ~InternalHeap (void) {}
  
  // Just one accessor.  Why? We don't want more than one (singleton)
  // and we want access to it neatly encapsulated here, for use by the
  // signal handler.
  static InternalHeap& getInstance (void) {
    static void * buf[sizeof(InternalHeap)];
    static InternalHeap * theOneTrueObject = new (buf) InternalHeap();
    return *theOneTrueObject;
  }
 
  void initialize(void) {
    _heap.initialize(xdefines::INTERNALHEAP_SIZE);
  }
 
  void checkMagic(void * ptr) {
    unsigned long * magicWord = (unsigned long *)((intptr_t)ptr - sizeof(objectHeader));

   if(*magicWord != 0xCAFEBABE) {
      fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@magicWord %lx at address %p with ptr %p@@@@@@@@@@@@@@@@@@@@@@\n", *magicWord, magicWord, ptr); 
    }
  }

  void * malloc (size_t sz) {
    void * ptr = NULL;
//		fprintf(stderr, "internalheap malloc with sz %lx\n", sz);
//		fprintf(stderr, "internalheap malloc with sz %lx threadindex %d\n", getThreadIndex(), sz);
    ptr = _heap.malloc (getThreadIndex(), sz);

  //  checkMagic(ptr);  
    return ptr;
  }
  
  void free (void * ptr) {
    _heap.free (getThreadIndex(), ptr);
//    checkMagic(ptr);  
  }
  
  void * malloc (int heapid, size_t sz) {
    void * ptr = NULL;
//		fprintf(stderr, "internalheap malloc with sz %lx\n", sz);
//		fprintf(stderr, "internalheap malloc with sz %lx threadindex %d\n", getThreadIndex(), sz);
    ptr = _heap.malloc (heapid, sz);

  //  checkMagic(ptr);  
    return ptr;
  }
  
  void free (int heapid, void * ptr) {
    _heap.free (heapid, ptr);
//    checkMagic(ptr);  
  }
  
private:
private:
  xpheap<xoneheap<SourceInternalHeap> >_heap; 
};


class InternalHeapAllocator {
public:
  static void * malloc (size_t sz) {
    return InternalHeap::getInstance().malloc(0, sz);
  }
  
  static void free (void * ptr) {
    return InternalHeap::getInstance().free(0, ptr);
  }
};

#endif
