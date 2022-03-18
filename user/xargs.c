#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
xargs() {

}

int 
main(int argc, char *argv[]) {
    // for (int i = )
    // exec(argv[1], arg);
    // exit(0);

    if (argc == 1) {
        fprintf(2, "usage: xargs ...\n");
        exit(1);
    }

    if (fork() == 0) {
        // exec(argv[1], 0);
        exit(0);
    }

    exit(0);
}