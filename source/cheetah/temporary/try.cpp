#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main()
{
    long tid;

    tid = syscall(SYS_gettid);
    printf("%ld\n", tid);
    return EXIT_SUCCESS;
}
