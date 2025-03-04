#include "types.h"
#include "user.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf(2, "Usage: chmod <file> <mode>\n");
        exit();
    }

    int mode = atoi(argv[2]);
    if (mode < 0 || mode > 7) {
        printf(2, "Invalid mode. Use a 3-bit number (0-7)\n");
        exit();
    }

    if (chmod(argv[1], mode) < 0) {
        printf(2, "Operation chmod failed\n");
    }
    exit();
}
