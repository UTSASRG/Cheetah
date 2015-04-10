/********************************************
 * Unoptimized matrix matrix multiplication *
 ********************************************/
#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <signal.h>
#include <perfmon/pfmlib.h>
#include <perfmon/pfmlib_amd64.h>
 
#include <perfmon/perfmon.h>
#include <perfmon/perfmon_dfl_smpl.h>
#include <fcntl.h>

#define MAX_THREAD 1024

#define NDIM 2000

#define MAX_FD 1024


double          a[NDIM][NDIM];
double          b[NDIM][NDIM];
double          c[NDIM][NDIM];

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
 
struct over_args* fd2ov[MAX_FD];

typedef struct
{
	int             id;
	int             noproc;
	int             dim;
	double	(*a)[NDIM][NDIM],(*b)[NDIM][NDIM],(*c)[NDIM][NDIM];
}               parm;

void start(struct over_args* ov)
{
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
  }
  ov->fd = fd;
  ov -> tid =gettid();
  fd2ov[fd]=ov;
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
    if (ret < 0){
    }
  } 
    
  if(pfm_self_start(ov->fd)==-1){
	printf("start error\n");
  } 
}

void end(struct over_args* ov)
{
	if(pfm_self_stop(ov->fd)==-1){
		printf("stop error\n");
  	}
}

void
sigio_handler(int sig, siginfo_t* siginfo, void* context)
{
	int fd = siginfo->si_fd;
	pfarg_msg_t msg;
//  	printf("fd =%d\n",fd);
	int i;
	if(fd2ov[fd]->tid!=gettid())
		printf("a mismatch occur\n");
	else
	{
		fd2ov[fd]->count++;
	}
//	pfm_read_pmds(fd, &fd2ov[fd]->pd[1], 1);
        if (read(fd, &msg, sizeof(msg)) != sizeof(msg))
                printf("read from sigio fd failed\n");
	if(pfm_restart(fd)){
		printf("restart error\n");
          exit(1);
        }
		
}

void mm(int me_no, int noproc, int n, double a[NDIM][NDIM], double b[NDIM][NDIM], double c[NDIM][NDIM])
{
	int             i,j,k;
	double sum;
	i=me_no;
	while (i<n) {

		for (j = 0; j < n; j++) {
			sum = 0.0;
			for (k = 0; k < n; k++) {
				sum = sum + a[i][k] * b[k][j];
			}
			c[i][j] = sum;

		}
		i+=noproc;
	}
}

void * worker(void *arg)
{
	struct over_args ov;
	memset(&ov,0,sizeof(ov));
	parm           *p = (parm *) arg;
	start(&ov);
	mm(p->id, p->noproc, p->dim, *(p->a), *(p->b), *(p->c));
	end(&ov);
	printf("fd = %d, count = %d\n", ov.fd, ov.count);
}


int main(int argc, char *argv[])
{
	int             j, k, noproc, me_no;
	double          sum;
	double          t1, t2;

	pthread_t      *threads;

	parm           *arg;
	int             n, i;
	struct over_args ov;
	struct sigaction sa;
	sigset_t set;

	memset(&ov, 0, sizeof(ov));

	for (i = 0; i < NDIM; i++)
		for (j = 0; j < NDIM; j++)
		{
			a[i][j] = i + j;
			b[i][j] = i + j;
		}
	for (i = 0; i < MAX_FD; i++) {
                fd2ov[i] = NULL;
        }

//	if (argc != 2)
//	{
//		printf("Usage: %s n\n  where n is no. of thread\n", argv[0]);
//		exit(1);
//	}
	n = 48;//atoi(argv[1]);

	if ((n < 1) || (n > MAX_THREAD))
	{
		printf("The no of thread should between 1 and %d.\n", MAX_THREAD);
		exit(1);
	}
	threads = (pthread_t *) malloc(n * sizeof(pthread_t));

	arg=(parm *)malloc(sizeof(parm)*n);

	memset(&sa, 0, sizeof(sa));
        sigemptyset(&set);
        sa.sa_sigaction = sigio_handler;
        sa.sa_mask = set;
        sa.sa_flags = SA_SIGINFO;
 
        if (sigaction(SIGIO, &sa, NULL) != 0)
                printf("sigaction failed");
 
        if (pfm_initialize() != PFMLIB_SUCCESS)
                printf ("pfm_initialize failed");
	/* setup barrier */

	/* Start up thread */

	/* Spawn thread */
	for (i = 0; i < n; i++)
	{
		arg[i].id = i;
		arg[i].noproc = n;
		arg[i].dim = NDIM;
		arg[i].a = &a;
		arg[i].b = &b;
		arg[i].c = &c;
		pthread_create(&threads[i], NULL, worker, (void *)(arg+i));
	}

	for (i = 0; i < n; i++)
	{
		pthread_join(threads[i], NULL);

	}
//	print_matrix(NDIM); 
//	check_matrix(NDIM);
	free(arg);
	return 0;
}

print_matrix(dim)
int dim;
{
	int i,j;

	printf("The %d * %d matrix is\n", dim,dim);
	for(i=0;i<dim;i++){
		for(j=0;j<dim;j++)
			printf("%lf ",  c[i][j]);
		printf("\n");
	}
}

check_matrix(dim)
int dim;
{
	int i,j,k;
	int error=0;

	printf("Now checking the results\n");
	for(i=0;i<dim;i++)
		for(j=0;j<dim;j++) {
			double e=0.0;

			for (k=0;k<dim;k++)
				e+=a[i][k]*b[k][j];

			if (e!=c[i][j]) {
				printf("(%d,%d) error\n",i,j);
				error++;
			}
		}
	if (error)
		printf("%d elements error\n",error);
		else
		printf("success\n");
}
