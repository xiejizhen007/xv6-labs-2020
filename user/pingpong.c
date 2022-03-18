#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    int fd0[2], fd1[2];
    pipe(fd0);     // p to c
    pipe(fd1);     // c to p

    char buff[10];

    int pid = fork();
    if (pid == 0) {
        close(fd0[1]);
        close(fd1[0]);

        read(fd0[0], buff, 4);
        printf("<%d>: received %s\n", getpid(), buff);
        write(fd1[1], "pong", 4);

        close(fd0[0]);
        close(fd1[1]);
        exit(0);
    } else {
        close(fd0[0]);
        close(fd1[1]);

        write(fd0[1], "pingpong", 8);
        
        int status;
        wait(&status);
        
        read(fd1[0], buff, 4);
        printf("<%d>: received %s\n", getpid(), buff);

        close(fd0[1]);
        close(fd1[0]);
        exit(0);
    }

    return 0;
}