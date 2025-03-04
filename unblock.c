#include "types.h"
#include "user.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf(2, "Usage: unblock <syscall_id>\n");
        exit();
    }

    int syscall_id = atoi(argv[1]);
    if (unblock(syscall_id) < 0) {
        printf(2, "Failed to unblock syscall %d\n", syscall_id);
    } else {
        printf(1, "Unblocked syscall %d\n", syscall_id);
    }
    exit();
}
