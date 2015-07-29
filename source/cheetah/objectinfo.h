// -*- C++ -*-

#ifndef SHERIFF_OBJECTINFO_H
#define SHERIFF_OBJECTINFO_H

#include "xdefines.h"

class ObjectInfo {
public:
  // Whether this is a heap object
  bool isHeapObject;
  bool hasPotentialFS;
  int  accessThreads;     // False sharing type, inter-objects or inner-object
  int  times; 
  unsigned long words;
  long invalidations;
  unsigned long totalWrites;
  unsigned long unitlength;
  unsigned long totallength;
  unsigned long totalFSAccesses; // the number of accesses on this object
	unsigned long totalFSCycles; // all cycles in this falsely-shared object.

	// Find information of involving threads.
	thread_t * threads[xdefines::MAX_ALIVE_THREADS];
	unsigned long totalThreads;
	unsigned long totalThreadsAccesses; // the number of accesses of related threads
	unsigned long totalThreadsCycles; // all cycles of related threads.
	unsigned long longestThreadRuntime; // the longest runtime of related threads, got by using rtdsc.
	unsigned long predictThreadsCycles;
	double threadReduceRate;
	double predictImprovement; //Final improvement 
	unsigned long realTotalRuntime;
	unsigned long predictTotalRuntime;

  unsigned long * start;
  unsigned long * stop;
  void * winfo;
  union {
    // Used for globals only.
    void * symbol;     
    
    // Callsite information.
    unsigned long callsite[CALL_SITE_DEPTH];
  } u;
};

#endif
