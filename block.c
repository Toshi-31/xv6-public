#include "types.h"
#include "user.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf(2, "Usage: block <syscall_id>\n");
        exit();
    }

    int syscall_id = atoi(argv[1]);
    if (block(syscall_id) < 0) {
        printf(2, "Failed to block syscall %d\n", syscall_id);
    } else {
        printf(1, "Blocked syscall %d\n", syscall_id);
    }
    exit();
}
