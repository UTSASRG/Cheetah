#include <sys/types.h>
#include <linux/unistd.h>
#include <errno.h>
#include <syscall.h>

#define gettid() syscall(__NR_gettid)

int main()
{
        gettid();
        return 0;
}
