/********************************************
 * Unoptimized matrix matrix multiplication *
 ********************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <signal.h>
#include <errno.h>
#include <perfmon/pfmlib.h>
#include <perfmon/pfmlib_amd64.h>
 
#include <perfmon/perfmon.h>
#include <perfmon/perfmon_dfl_smpl.h>
#include <fcntl.h>

#define MAX_THREAD 1024

#define NDIM 2000

#define MAX_FD 1024

extern "C" void handleAccess(pid_t tid, unsigned long addr, size_t size, bool isWrite);

pid_t
gettid(void)
{
        return (pid_t)syscall(__NR_gettid);
}

void
sigio_handler(int sig, siginfo_t *siginfo, void *context);

struct over_args {
        pfmlib_amd64_input_param_t inp_mod;
        pfmlib_output_param_t outp;
        pfmlib_amd64_output_param_t outp_mod;
        pfarg_pmc_t pc[PFMLIB_MAX_PMCS];
        pfarg_pmd_t pd[PFMLIB_MAX_PMDS];
        pfarg_ctx_t ctx;
        pfarg_load_t load_args;
        int fd;
        pid_t tid;
        pthread_t self;
        int count;
};
 
struct over_args fd2ov[MAX_FD];


void startIBS(int threadid)
{
  printf("Begin to start IBS %d\n", threadid);

  over_args *ov = &fd2ov[threadid];
  if (ov->fd) pfm_unload_context(ov->fd);
  memset(ov, 0, sizeof(struct over_args));

  int period=(65535<<4);
  ov->inp_mod.ibsop.maxcnt=period;
  ov->inp_mod.ibsop.options = IBS_OPTIONS_UOPS;
  ov->inp_mod.flags |= PFMLIB_AMD64_USE_IBSOP;
  int ret;
  ret=pfm_dispatch_events(NULL, &ov->inp_mod, &ov->outp, &ov->outp_mod);
  if (ret != PFMLIB_SUCCESS) {
    exit(1);
  }
  if (ov->outp.pfp_pmc_count != 1) {
    exit(1);
  }
  if (ov->outp.pfp_pmd_count != 1) {
    exit(1);
  }
  if (ov->outp_mod.ibsop_base != 0) {
    exit(1);
  }
 
  ov->pc[0].reg_num=ov->outp.pfp_pmcs[0].reg_num;
  ov->pc[0].reg_value=ov->outp.pfp_pmcs[0].reg_value;
 
  ov->pd[0].reg_num=ov->outp.pfp_pmds[0].reg_num;//pd[0].reg_num=7
  ov->pd[0].reg_value=0;
 
  ov->pd[0].reg_flags |= PFM_REGFL_OVFL_NOTIFY;
 
  ov->pd[0].reg_smpl_pmds[0]=((1UL<<7)-1)<<ov->outp.pfp_pmds[0].reg_num;
  int fd = pfm_create_context(&ov->ctx, NULL, 0, 0);
  if(fd==-1){
    printf("cannot create context\n");
  }
  ov->fd = fd;
  ov->tid =gettid();
  printf("tid is %d\n", ov->tid);
  if(pfm_write_pmcs(ov->fd,ov->pc,ov->outp.pfp_pmc_count)){
	printf("write pmc error\n");
  }
 
  if(pfm_write_pmds(ov->fd,ov->pd,ov->outp.pfp_pmd_count)){
	printf("write pmd error\n");
  }
 
  ov->load_args.load_pid=gettid();
 
  if(pfm_load_context(ov->fd, &ov->load_args)){
  }
 
  ret = fcntl(ov->fd, F_SETFL, fcntl(ov->fd, F_GETFL, 0) | O_ASYNC);
  if (ret == -1){
    printf("fcntl F_SETFL return -1\n");
  }

  #ifndef F_SETOWN_EX
  #define F_SETOWN_EX     15
  #define F_GETOWN_EX     16

  #define F_OWNER_TID     0
  #define F_OWNER_PID     1
  #define F_OWNER_GID     2

  struct f_owner_ex {
        int     type;
        pid_t   pid;
  };
  #endif
  {
    struct f_owner_ex fown_ex;
    fown_ex.type = F_OWNER_TID;
    fown_ex.pid  = gettid();
    ret = fcntl(ov->fd, F_SETOWN_EX, &fown_ex);
    if (ret == -1){
      printf("fcntl F_SETOWN return -1\n");
   }

   ret = fcntl(ov->fd, F_SETSIG, SIGIO);
     if(ret < 0){
       printf("cannot set SIGIO\n");
     }
   } 
    
  if(pfm_self_start(ov->fd)==-1){
	printf("start error with fd %d, %s\n", ov->fd, strerror(errno));
  } 
}
 
void stopIBS(int nthreads)
{
  printf("Begin to stop IBS\n");
  int i;
  for (i=0; i<nthreads; i++) {
	if(pfm_self_stop(fd2ov[i].fd)==-1){
		printf("stop error for %d\n", fd2ov[i].fd);
  	}
  }
}

void
sigio_handler(int sig, siginfo_t* siginfo, void* context)
{
	void *ip = NULL;
	ibsopdata2_t *opdata2;
        ibsopdata3_t *opdata3;
	uint64_t data3 = 0;
	uint64_t linear_addr = 0;
	int lat = 0;

	int fd = siginfo->si_fd;
	pfarg_msg_t msg;
//  	printf("fd =%d\n",fd);
	int i;
	int threadid = -1;
        for (i=0; i<MAX_FD ; i++) {
	  if (fd2ov[i].fd == fd) {
	    threadid = i;
	    break;
          }
   	}
	struct over_args *ov = &fd2ov[i];
	if(ov->tid!=gettid())
		printf("a mismatch occur\n");
	else
	{
		ov->count++;
	}
//	pfm_read_pmds(fd, &fd2ov[fd]->pd[1], 1);
        if (read(fd, &msg, sizeof(msg)) != sizeof(msg))
                printf("read from sigio fd failed\n");

	if ((threadid >= 0) && (msg.type == PFM_MSG_OVFL)) {
	  ov->pd[0].reg_num = 8;
	  pfm_read_pmds(fd, ov->pd, 1);
	  ip = (void *)ov->pd[0].reg_value;

	  ov->pd[0].reg_num = 11;
	  pfm_read_pmds(fd, ov->pd, 1);
	  data3 = ov->pd[0].reg_value;
	  opdata3 = (ibsopdata3_t *)(&data3);
	  if(((opdata3->reg.ibsldop == 1) || (opdata3->reg.ibsstop == 1)) && (opdata3->reg.ibsdclinaddrvalid == 1))
          {
	    ov->pd[0].reg_num = 12;
	    pfm_read_pmds(fd, ov->pd, 1);
	    linear_addr = ov->pd[0].reg_value;
	    // exclude kernel addresses
   	    if (!((linear_addr >> 63) & 0x1)) {
              if (opdata3->reg.ibsstop) {
	        handleAccess(threadid, (unsigned long)linear_addr, 1, true);
//	        printf("store: addr is %x, thread id is %d\n", linear_addr, threadid);
              }
              else {
	        handleAccess(threadid, (unsigned long)linear_addr, 1, false);
//	        printf("load: addr is %x, thread id is %d\n", linear_addr, threadid);
	      }
            }
	  }
	  if((opdata3->reg.ibsldop == 1) && (opdata3->reg.ibsdcmiss == 1)){
	    lat = opdata3->reg.ibsdcmisslat;
	  }
        }

	if(pfm_restart(fd)){
		printf("restart error\n");
          exit(1);
        }
		
}

void initIBS()
{
	int             n, i;
	struct over_args ov;
	struct sigaction sa;
	sigset_t set;

	printf("Begin to initialize IBS\n");
	memset(&ov, 0, sizeof(ov));

	memset(&sa, 0, sizeof(sa));
        sigemptyset(&set);
        sa.sa_sigaction = sigio_handler;
        sa.sa_mask = set;
        sa.sa_flags = SA_SIGINFO;
 
        if (sigaction(SIGIO, &sa, NULL) != 0)
                printf("sigaction failed");
 
        if (pfm_initialize() != PFMLIB_SUCCESS)
                printf ("pfm_initialize failed");
}
