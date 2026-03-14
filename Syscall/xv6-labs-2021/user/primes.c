#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void sieve(int fd) {
    int target;
    if (read(fd, &target, 4) == 0) {
        exit(0);
    }

    fprintf(1, "prime %d\n", target);

    int fd_p2c[2];
    pipe(fd_p2c);

    int pid = fork();

    if (pid == 0) {
        close(fd_p2c[1]);
        sieve(fd_p2c[0]);
        close(fd_p2c[0]);
        exit(0);
    }
    else if (pid > 0) {
        int num;
        close(fd_p2c[0]);
        while (read(fd, &num, 4) > 0) {
            if (num % target != 0) {
                write(fd_p2c[1], &num, 4);
            }
        }
        close(fd);
        close(fd_p2c[1]);
        wait(0);
        exit(0);
    }
    else {
        fprintf(2, "primes: fork error\n");
        exit(1);
    }
}
int main(int argc, char** argv) {
    int fd[2];
    pipe(fd);

    int pid = fork();
    if (pid == 0) {
        close(fd[1]);
        sieve(fd[0]);
        close(fd[0]);
        exit(0);
    }
    else if (pid > 0) {
        close(fd[0]);
        for (int i = 2;i <= 35;i++) {
            write(fd[1], &i, 4);
        }
        close(fd[1]);
        wait(0);
        exit(0);
    }
    else {
        fprintf(2, "primes: fork error\n");
        exit(1);
    }

}
