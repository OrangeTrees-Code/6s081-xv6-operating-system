#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main()
{
    int fd_p2c[2];
    pipe(fd_p2c);
    int fd_c2p[2];
    pipe(fd_c2p);
    char bytes[1] = {'a'};
    int pid;
    pid = fork();
    if (pid == 0)
    {
        close(fd_p2c[1]);
        close(fd_c2p[0]);

        read(fd_p2c[0], bytes, 1);
        fprintf(1, "%d: received ping\n", getpid());
        write(fd_c2p[1], bytes, 1);

        close(fd_p2c[0]);
        close(fd_c2p[1]);
        exit(0);
    }
    else if (pid > 0)
    {
        close(fd_p2c[0]);
        close(fd_c2p[1]);

        write(fd_p2c[1], bytes, 1);
        read(fd_c2p[0], bytes, 1);
        fprintf(1, "%d: received pong\n", getpid());

        close(fd_p2c[1]);
        close(fd_c2p[0]);
        exit(0);
    }
    else
    {
        fprintf(2, "fork error\n");
        exit(1);
    }
}