#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: sleep 1 \n");
        exit(1);
    }

    int secs = atoi(argv[1]);
    if (secs > 0) {
        int sleepSecs = sleep(secs);
        fprintf(2, "sleep time: %d.\n", secs - sleepSecs);
    } else {
        fprintf(2, "Usage: sleep don't support negative num. \n");
        exit(1);
    }

    exit(0);
}