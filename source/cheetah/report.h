// -*- C++ -*-

#ifndef _REPORT_H_
#define _REPORT_H_

#include <set>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "xdefines.h"
#include "mm.h"
#include "objectinfo.h"
#include "objectheader.h"
#include "elfinfo.h"
#include "callsite.h"
#include "cachetrack.h"
#include "xthread.h"

class report {
  enum { MAXBUFSIZE = 4096 };
public:

  report() { }

  void initialize(bool isHeap, void * start, void * end, void * cacheWrites, cachetrack ** cacheTrackings) 
  {
    int    count;
    void * ptr;

//		fprintf(stderr, "report initialize now now!\n");
    if(start == NULL) {
      fprintf(stderr, "start is 0\n");
      while(1) ;
    }
    assert(start != NULL);
    assert(end != NULL);
    assert(cacheWrites != NULL);
		
 //   fprintf(stderr, "reporting start %p end %p cacheWrites %p cacheTrackings %p totalFSCycles %ld totalFSAccesses %ld\n", start, end, cacheWrites, cacheTrackings, latency, accesses);

    _isHeap = isHeap;
    _start = (char *)start;
    _end = (char *)end;
    _cacheWrites = (unsigned long *)cacheWrites;  
    _cacheTrackings = cacheTrackings; 
     
    if(_start > _end) {
      fprintf(stderr, "Start %p end %p\n", _start, _end);
    } 
    assert(_start <= _end);

    /* Get current executable file name. */
    count = readlink("/proc/self/exe", _curFilename, MAXBUFSIZE);
    if (count <= 0 || count >= MAXBUFSIZE)
    {
      fprintf(stderr, "Failed to get current executable file name\n" );
      exit(1);
    }
    _curFilename[count] = '\0';

    /* Get text segmentations information. */
    _elfInfo.hdr = (Elf_Ehdr*)mapImage(_curFilename, &_elfInfo.size);
    if (!_elfInfo.hdr) {
      fprintf(stderr, "Can't grab file %s\n", _curFilename);
      exit(1);
    }
  
    parseElfFile();
  }

   ~report() {
  }

  // Mapping the specified elf file to current process and return the starting address.
  void * mapImage(const char *filename, unsigned long *size) {
    struct stat st;
    void *map;
    int fd;

    fd = open(filename, O_RDONLY);
    if (fd < 0 || fstat(fd, &st) != 0)
      return NULL;

    *size = st.st_size;

    // Mapping current image to 
    map = MM::mmapAllocatePrivate(*size, NULL, fd);
    close(fd);

    if (map == MAP_FAILED)
      return NULL;
    return map;
  }

  // Verify the elf file is an valid elf file.
  bool isValidElfFile(struct elf_info * _elfInfo) 
  {
    Elf_Ehdr *hdr = _elfInfo->hdr;

    if (_elfInfo->size < sizeof(*hdr)) {
        /* file too small, assume this is an empty .o file */
        return false;
    }
    
    /* Is this a valid ELF file? */
    if ((hdr->e_ident[EI_MAG0] != ELFMAG0) ||
       (hdr->e_ident[EI_MAG1] != ELFMAG1) ||
       (hdr->e_ident[EI_MAG2] != ELFMAG2) ||
       (hdr->e_ident[EI_MAG3] != ELFMAG3)) {
      /* Not an ELF file - silently ignore it */
      return false;
   }

   return true;
  }

  int parseElfFile()
  {
    unsigned int i;
    Elf_Ehdr *hdr = _elfInfo.hdr;
    Elf_Shdr *sechdrs;
  
    if(!isValidElfFile(&_elfInfo)) {
      fprintf (stderr, "Erroneous ELF file.\n");
      exit(-1);
    }
  

    sechdrs =(Elf_Shdr *)((char *)hdr + hdr->e_shoff);
    _elfInfo.sechdrs = sechdrs;

    /* Fix endianness in section headers */
    for (i = 0; i < hdr->e_shnum; i++) {
      if (sechdrs[i].sh_type != SHT_SYMTAB)
        continue;
  
      if(sechdrs[i].sh_size == 0) 
        continue;
  
      _elfInfo.symtab_start = (Elf_Sym*)((unsigned long)hdr + sechdrs[i].sh_offset);
      _elfInfo.symtab_stop  = (Elf_Sym*)((unsigned long)hdr + sechdrs[i].sh_offset + sechdrs[i].sh_size);
      _elfInfo.strtab       = (char *)((unsigned long)hdr + sechdrs[sechdrs[i].sh_link].sh_offset);
      break;
    }
  
    return 0; 
  }

  /*
   * Find symbols according to specific address.  
   */
  Elf_Sym * find_symbol(struct elf_info *elf, Elf_Addr addr)
  {
    int bFound = 0;
    Elf_Sym *symbol;
    Elf_Ehdr *hdr = elf->hdr;
  
    for (symbol = elf->symtab_start; symbol < elf->symtab_stop; symbol++) {
      if (symbol->st_shndx >= SHN_LORESERVE)
        continue;
      
      /* Checked only when it is a global variable. */
      if(ELF_ST_BIND(symbol->st_info) != STB_GLOBAL || ELF_ST_TYPE(symbol->st_info) != STT_OBJECT)
        continue;
  
      /* If the addr is in the range of current symbol, that get it. */
      if(addr >= symbol->st_value && addr < (symbol->st_value + symbol->st_size)) {
        bFound = 1;
        break;
      }
    }
  
    if(bFound)
      return symbol;
    else
      return NULL;
  }

  void reportFalseSharing(void) {
    if(_isHeap) {
      reportHeapObjects();
    }
    else {
      reportGlobalObjects();
    }
  }
  
  
  inline bool hasFSSuspect(unsigned long * status) {
    return (*status > xdefines::THRESHOLD_TRACK_DETAILS);
  }

  inline bool isFSSuspect(unsigned long writes) {
    return writes > xdefines::THRESHOLD_TRACK_DETAILS ? true : false;
  }

  
  inline struct wordinfo * allocWordinfo(int words) {
    struct wordinfo * winfo = (struct wordinfo *)InternalHeap::getInstance().malloc(sizeof(struct wordinfo) * words);
 //   fprintf(stderr, "allocWordinfo %p\n", winfo); 
    if(winfo != NULL) {
      memset(winfo, 0, sizeof(struct wordinfo) * words);
    }

    return winfo;
  }

  //inline bool getCacheInvalidations(unsigned long * cacheWrites, int lines, ObjectInfo * object) {
  inline bool getCacheInvalidations(unsigned long cacheindex, int lines, ObjectInfo * object) {
    bool hasFS = false;
    unsigned long firstOffset = getCachelineOffset((unsigned long)object->start);
    unsigned long lastOffset = getCachelineOffset((unsigned long)object->stop);
    cachetrack * track = NULL;
    struct wordinfo * winfo = NULL;
    int windex = 0;
		unsigned long latency = 0;
  
    object->winfo = NULL;
    object->invalidations = 0;
    object->totalWrites = 0;
    object->totalFSAccesses = 0;
    object->totalFSCycles = 0;

    assert(firstOffset < CACHE_LINE_SIZE);

//     fprintf(stderr, "getCacheInvalidations firstOffset %lx lastOffset %lx lines %d\n", firstOffset, lastOffset, lines);
    // Traverse all related cache lines.
    for(unsigned long i = cacheindex; i < (cacheindex+lines); i++) {
      unsigned long index;
      unsigned long words;

      if(lines == 1) {
        index = firstOffset/xdefines::WORD_SIZE;
        // Justify the last offset, it should not be 0 since it can point to last bytes of a cache line. 
        if(lastOffset == 0) {
          lastOffset = CACHE_LINE_SIZE;
        }
        words = (lastOffset - firstOffset)/xdefines::WORD_SIZE;
      }
      else {
        if(i == cacheindex) {
          index = firstOffset/xdefines::WORD_SIZE;
          words = (CACHE_LINE_SIZE-firstOffset)/xdefines::WORD_SIZE; 
        }
        else if(i == cacheindex + lines - 1) {
          index = 0;
          words = lastOffset/xdefines::WORD_SIZE;
        }
        else {
          index = 0;
          words = CACHE_LINE_SIZE/xdefines::WORD_SIZE;
        }
      }
      
      // We only have false sharing when the cache status is larger than predefined threshold
     //fprintf(stderr, "cache %d: cacheWrites is %ld  at %p\n", i, _cacheWrites[i], &_cacheWrites[i]);
      if(_cacheWrites[i] >= xdefines::THRESHOLD_TRACK_DETAILS) {
        track = (cachetrack *)_cacheTrackings[i];

        if(track == NULL) {
          continue;
        }

				// Detailed report, maybe we should close it in the future.
        if(track->getInvalidations() > 0) {
          //fprintf(stderr, "cache %ld: cacheWrites is %ld invalidations %lx at %p\n", i, _cacheWrites[i], track->getInvalidations(), track);
          track->reportFalseSharing();
        }
        
        if(winfo == NULL) {
          winfo = allocWordinfo(object->words);
        }

        memcpy(&winfo[windex], track->getWordinfoAddr(index), words * sizeof(struct wordinfo));

        object->totalWrites += track->getWrites();
        if(track->getInvalidations() > 0) { 
          object->invalidations += track->getInvalidations();
        }

				object->totalFSCycles += track->getLatency();
        object->totalFSAccesses += track->getAccesses();
      }
   
      
      windex += words;
    }
 
    if(winfo) {
      object->winfo = (void *)winfo;
    }
 
		// Get the threads related information.
		unsigned long checkedWords = 0;

		// Cleaning up this threads.
		memset(&object->threads, 0, xdefines::MAX_ALIVE_THREADS * sizeof(pid_t));
		object->totalThreadsAccesses = 0;
		object->totalThreadsCycles = 0;
		object->totalThreads = 0;
		object->longestThreadRuntime = 0;
		
		while(winfo) {
			pid_t tid = winfo->tid;
			bool inside = false;
			
			// Check whether this tid is recorded or not.
			for(int i = 0; i < object->totalThreads; i++) {
				if(tid == object->threads[i]->tid) {
					inside = true;
					break;
				}
			} 

			if(inside == false && (tid != cachetrack::WORD_THREAD_SHARED)) {
				// Finding thread_t of this thread. 
				thread_t * thisThread = xthread::getInstance().getThreadInfoByTid(tid);

				if(thisThread != NULL) {
					// If this thread is not inside, record this threads.
					object->threads[object->totalThreads] = thisThread;
		
//					fprintf(stderr, "tid is %d\n", tid);	
					object->totalThreadsAccesses += thisThread->accesses;
					object->totalThreadsCycles += thisThread->latency;
				
					if(object->longestThreadRuntime < thisThread->actualRuntime) {
						object->longestThreadRuntime = thisThread->actualRuntime;
					} 

					object->totalThreads++;
				}	
			}

			// We will check next word
			checkedWords++;

			if(checkedWords == object->words && object->totalThreads > 0) {
				// Now we have traversed all words.
				thread_t * initialThread = xthread::getInstance().getThreadInfoByIndex(0);

				unsigned long cyclesWithoutFS = (initialThread->latency * 100)/initialThread->accesses;
				
				// Now we can compute the performance improvement.
				object->predictThreadsCycles = object->totalThreadsCycles - object->totalFSCycles + ((object->totalFSAccesses * cyclesWithoutFS)/100);
				double threadImprove = (double)(object->predictThreadsCycles)/(double)object->totalThreadsCycles;
				object->threadReduceRate = threadImprove;
							
				// Check whether involved threads are on the critical path of correponding thread level.
				unsigned long realTotalRuntime = 0;
				unsigned long predictTotalRuntime = 0;
				unsigned long threadLevels = xthread::getInstance().getTotalThreadLevels();

				// We actually check all levels.
				for(int i = 0; i <= threadLevels; i++) {
					struct threadLevelInfo * levelinfo = xthread::getInstance().getThreadLevelByIndex(i);
				
					// Now check whether this level is serial phase or not.
					realTotalRuntime += levelinfo->elapse;
					if(levelinfo->beginIndex == levelinfo->endIndex) {
						// In this situation, index is not actual thread index at all. 
						predictTotalRuntime += levelinfo->elapse;
					}
					else {
						// Predict how much the performance can be affected by the possible improvement.
						// In all threads of this level, find out the longest involving thread and 
						// the longest nonrelated thread.
						unsigned long involvedLongestRuntime = 0;
						unsigned long nonrelatedLongestRuntime = 0;
						for(int j = levelinfo->beginIndex; j <= levelinfo->endIndex; j++) {
							thread_t * thisThread = xthread::getInstance().getThreadInfoByIndex(j);
							bool isFound = false;

							// Compare this thread with all involved threads
							for(int k = 0; k < object->totalThreads; k++) {
								if(thisThread == object->threads[k]) {
									isFound = true;
									break;
								}
							}
								
							if(isFound && (involvedLongestRuntime < thisThread->actualRuntime)) {
								involvedLongestRuntime = thisThread->actualRuntime;
							}
							else if((isFound == false) && (nonrelatedLongestRuntime < thisThread->actualRuntime)) {
								nonrelatedLongestRuntime = thisThread->actualRuntime;
							}
						} 	
					
						// Compute the predict runtime.
						unsigned long predictThreadRuntime = (unsigned long)((double)involvedLongestRuntime * threadImprove);
						if(predictThreadRuntime > nonrelatedLongestRuntime) {
							predictTotalRuntime += predictThreadRuntime;
						}
						else {
							predictTotalRuntime += nonrelatedLongestRuntime;
						} 	
					}	

				}
				
				object->predictImprovement = ((double)realTotalRuntime - (double)predictTotalRuntime)/(double)realTotalRuntime; 
				fprintf(stderr, "real totalRuntime %ld predicted TotalRuntime %ld\n", realTotalRuntime, predictTotalRuntime);	
		//		fprintf(stderr, "initialthread cycles %ld predictCycles %ld actualcycles %ld threadImprove %f predicting improvement %f\n", cyclesWithoutFS, object->predictThreadsCycles, object->totalThreadsCycles, threadImprove, object->predictImprovement); 	
				break;
			}
			
			// Check next word
			winfo++;
		}	

    return hasFS; 
  }

	// Check whether this object is a valid heap object since the data of 
	// an object may have the same signature as the magic value of an object 
	bool isInvalidIdentification(char * objectEnd) {
  	bool invalid = false;

  	if(objectEnd > _end) {
    	invalid = true;
  	}
  	else if (objectEnd < _end && (*((unsigned long *)objectEnd) != objectHeader::MAGIC)) {
   		invalid = true;
  	}
  	return invalid;
	}

  inline size_t getCachelineOffset(unsigned long addr) {
    return (addr - getCacheStart((void *)addr));
  }

  // Search a global object with 
  int reportHeapObjects(void) {
    unsigned long memstart = (unsigned long)_start;
    unsigned long * memend = (unsigned long *)_end; 
    unsigned long * pos = (unsigned long *)_start;

//    fprintf(stderr, "reportHeapObjects pos %p memend %p\n", pos, memend); 
    while(pos < memend) {
      // We are tracking word-by-word to find the objec theader.  
      if(*pos == objectHeader::MAGIC) {
        objectHeader * object = (objectHeader *)pos;
        unsigned long  objectStart = (unsigned long)object->getObjectStart();
        unsigned long  objectOffset = objectStart;
        unsigned long  objectEnd = (unsigned long)object->getObjectEnd();
        unsigned long  cacheStart = getCacheStart((void *)objectStart);
        
        // Checking whether the identification of an object is valid or not
        if(isInvalidIdentification((char *) objectEnd)) {
            pos++;
            continue;
        }
        // Calculate how many cache lines are occupied by this object.
        int   cachelineIndex = getCacheline(objectOffset); 
//      fprintf(stderr, "cacheStart %lx cacheineIndex %d now\n", cacheStart, cachelineIndex);
        int   lines = getCoveredCachelines(cacheStart, objectEnd);
 
//    	fprintf(stderr, "reportHeapObjects pos %p memend %p\n", pos, memend); 
        // Check whether there are some invalidations on this object.
        bool hasFS = false;
        ObjectInfo objectinfo;
        objectinfo.isHeapObject = true;
        objectinfo.winfo = NULL;
        objectinfo.unitlength = object->getSize();
        objectinfo.words = objectinfo.unitlength/xdefines::WORD_SIZE;
        objectinfo.start = (unsigned long *)objectStart;
        objectinfo.stop = (unsigned long *)objectEnd;

        hasFS = getCacheInvalidations(cachelineIndex, lines, &objectinfo);
 
//    	fprintf(stderr, "reportHeapObjects pos %p memend %p\n", pos, memend); 
        // Whenever interleaved writes is larger than the specified threshold
        if(objectinfo.invalidations > xdefines::THRESHOLD_REPORT_INVALIDATIONS) {
          // Save object information.
          memcpy((void *)&objectinfo.u.callsite, object->getCallsiteAddr(), sizeof(CallSite));
          reportFalseSharingObject(&objectinfo);
        }
        
        if(objectinfo.winfo) {
         // fprintf(stderr, "objectinfo.winfo %p before free\n", objectinfo.winfo);
          InternalHeap::getInstance().free(objectinfo.winfo);
        }
          
        pos = (unsigned long *)objectEnd;
        continue;
      } 
      else {
        pos++;
      }
    }
  }

  int reportGlobalObjects(void) {
    struct elf_info *elf = &_elfInfo;  
    Elf_Ehdr *hdr = elf->hdr;
    Elf_Sym *symbol;

   fprintf(stderr, "reportGlobalObjects now. globalstart %lx globalend %lx\n", globalStart, globalEnd);
    for (symbol = elf->symtab_start; symbol < elf->symtab_stop; symbol++) {
      if(symbol == NULL) {
        continue;
      }

      if (symbol->st_shndx >= SHN_LORESERVE)
        continue;

      /* Checked only when it is a global variable. */
      if(ELF_ST_BIND(symbol->st_info) != STB_GLOBAL || ELF_ST_TYPE(symbol->st_info) != STT_OBJECT)
        continue;

      // Now current symbol is one normal object. 
      unsigned long objectStart = symbol->st_value;
      
      size_t objectSize = symbol->st_size;

      size_t objectOffset = objectStart;
      unsigned long objectEnd = objectStart + objectSize;
  
      //if(objectStart < globalStart || objectStart > globalEnd || objectEnd > globalEnd) {
      if(objectStart < globalStart || objectStart > globalEnd) {
        continue;
      }

      unsigned long cacheStart = getCacheStart((void *)objectStart);
      int   cachelineIndex = getCacheline(objectOffset);
 
     unsigned long lines = getCoveredCachelines(cacheStart, objectStart + objectSize);
      // Check whether there are some invalidations on this object.
      bool hasFS = false;
      ObjectInfo objectinfo;
      objectinfo.isHeapObject = false;
      objectinfo.unitlength = objectSize;
      objectinfo.words = objectSize/xdefines::WORD_SIZE;
      objectinfo.start = (unsigned long *)objectStart;
      objectinfo.stop = (unsigned long *)objectEnd;
         
      hasFS = getCacheInvalidations(cachelineIndex, lines, &objectinfo);

      // For globals, only when we need to output this object then we need to store that.
      // Since there is no accumulation for global objects.
      if(objectinfo.invalidations > xdefines::THRESHOLD_REPORT_INVALIDATIONS) {
        // Check how many objects are located in the last cache line.
        objectinfo.u.symbol = (void *)symbol;
        reportFalseSharingObject(&objectinfo);
      }
        
      if(objectinfo.winfo) {
        InternalHeap::getInstance().free(objectinfo.winfo);
      }
    }
  }

  inline unsigned long getCachelineIndex(unsigned long addr) {
    return (addr >> CACHE_LINE_SIZE_SHIFTS);
  }


  // Print out the detail object information about false sharing objects. 
  void reportFalseSharingObject(ObjectInfo * object) {
    char buf[MAXBUFSIZE];
    int ret; 
    int words = object->unitlength/xdefines::WORD_SIZE;
    int size; 

		// We should check the latency of an object
    fprintf(stderr, "filename %s\n", _curFilename);
    fprintf(stderr, "FALSE SHARING: start %p end %p (with size %lx). Accesses %lx invalidations %lx writes %lx total latency %lx.\n", object->start, object->stop, object->unitlength, object->totalFSAccesses, object->invalidations, object->totalWrites, object->totalFSCycles);
    fprintf(stderr, "Latency information: totalThreads %ld totalThreadsAccesses %lx totalThreadsCycles %lx longestRuntime %ld thread reduce rate %f total improvement rate %f\n\n", object->totalThreads, object->totalThreadsAccesses, object->totalThreadsCycles, object->longestThreadRuntime, object->threadReduceRate, object->predictImprovement);
    if(object->isHeapObject) { 
    fprintf(stderr, "It is a HEAP OBJECT with callsite stack:\n");
      for(int i = 0; i < CALL_SITE_DEPTH; i++) {
        if(object->u.callsite[i] != 0) {
//          sprintf(buf, "addr2line -i -e %s %lx", _curFilename, object->u.callsite[i]);
          fprintf(stderr, "callsite: %lx\n", object->u.callsite[i]);
//          ret = system(buf);
        }
      }
    }
    else {
    	fprintf(stderr, "It is a GLOBAL VARIABLE:\n");
      // Print object information about globals.
      Elf_Sym *symbol = (Elf_Sym *)object->u.symbol;
//find_symbol(&_elfInfo, (intptr_t)object->start);
      if(symbol != NULL) {
        const char * symname = _elfInfo.strtab + symbol->st_name;
        fprintf(stderr, "\tGlobal object: name \"%s\", start %lx, size %ld\n", symname, symbol->st_value, symbol->st_size);
      }
    }

    size = object->unitlength;
   
    #ifdef OUTPUT_WORD_ACCESSES
    fprintf(stderr, "\n");
    int * address = (int *)object->start;
    struct wordinfo * winfo = (struct wordinfo *) object->winfo;
    for(int i = 0; i < size/xdefines::WORD_SIZE; i++) {
      if((winfo[i].reads + winfo[i].writes) != 0) {
        //fprintf(stderr, "\tWord %d: at address %p (line %d), reads %d writes %d ", i, address, winfo[i].reads, winfo[i].writes); 
        fprintf(stderr, "\tAddress %p (line %ld): reads %d writes %d ", address, getCachelineIndex((intptr_t)address), winfo[i].reads, winfo[i].writes); 
        if(winfo[i].tindex == cachetrack::WORD_THREAD_SHARED) {
          fprintf(stderr, "by mulitple thread\n");
        }
        else {
          fprintf(stderr, "by thread %d\n", winfo[i].tindex);
        } 
      }
      address++;
    }
    #endif 
    fprintf(stderr, "\n\n");
  }

private:

  struct elf_info _elfInfo;

  // Profiling type.
  bool _isHeap;

  char * _start; 
  char * _end;
  unsigned long * _cacheWrites;
  cachetrack ** _cacheTrackings;

	// Runtime of the whole process with different phases.
  char _curFilename[MAXBUFSIZE];
};


#endif
