#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define BUFSIZE 512

char buf[BUFSIZE];

int
readline(char *cur_argv[MAXARG], int cur_argc) {
    int n = 0;
    while (read(0, buf + n, 1) != 0) {
        if (buf[n] == '\n') {
            break;
        }

        n++;

        if (n >= BUFSIZE) {
            fprintf(2, "argument is too long\n");
            exit(0);
        }
    }

    // 结尾
    buf[n] = 0;

    if (n == 0) {
        // 没数据了
        return 0;
    }

    int p = 0;
    while (p < n) {
        cur_argv[cur_argc++] = buf + p;
        while (p < n && buf[p] != ' ') {
            p++;
        }

        while (p < n && buf[p] == ' ') {
            // 无用，清理上一行留下的
            buf[p++] = 0;
        }

        // printf("argv: %s\n", cur_argv[cur_argc - 1]);
    }

    return cur_argc;
}

int 
main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "usage: xargs ...\n");
        exit(1);
    }

    // for (int i = 0; argv[i]; i++) {
    //     printf("argv: %s\n", argv[i]);
    // }

    // 需要执行的命令
    char *command = (char *)malloc(strlen(argv[1]) + 1);
    strcpy(command, argv[1]);

    char *cur_argv[MAXARG];
    for (int i = 1; i < argc; i++) {
        cur_argv[i - 1] = malloc(strlen(argv[i]) + 1);
        strcpy(cur_argv[i - 1], argv[i]);
    }

    int cur_argc = 0;
    while ((cur_argc = readline(cur_argv, argc - 1)) != 0) {
        cur_argv[cur_argc] = 0;
        if (fork() == 0) {
            exec(command, cur_argv);
            printf("xargs: exec is failed\n");
            exit(1);
        }

        wait(0);
    }

    exit(0);
}