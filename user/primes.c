#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
work(int p[2]) {
    // close write fd
    close(p[1]);

    // int: 4 byte
    char buf[4];

    // read from pipe
    if (read(p[0], buf, 4) != 4) {
        printf("error: faild to read\n");
        exit(1);
    }

    int cur = *((int *)buf);
    printf("prime: %d\n", cur);

    int newPipe[2];
    int flag = 0;

    while (read(p[0], buf, 4) == 4) {
        int n = *((int *)buf);
        if ((n % cur) != 0) {
            if (flag == 0) {
                pipe(newPipe);
                if (fork() == 0) {
                    work(newPipe);
                }

                close(newPipe[0]);
                flag = 1;
            }

            write(newPipe[1], &n, 4);
        }
    }

    close(p[0]);
    close(newPipe[1]);

    while (wait(0) != -1) {}
    exit(0);
}

int 
main(int argc, char *argv[]) {
    int p[2];
    pipe(p);

    if (fork() == 0) {
        work(p);
    } else {
        close(p[0]);
        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, 4);
        }
        close(p[1]);
        wait(0);
    }

    exit(0);
}