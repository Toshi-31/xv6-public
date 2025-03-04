// // init: The initial user-level program

// #include "types.h"
// #include "stat.h"
// #include "user.h"
// #include "fcntl.h"

// char *argv[] = { "sh", 0 };

// int
// main(void)
// {
//   int pid, wpid;

//   if(open("console", O_RDWR) < 0){
//     mknod("console", 1, 1);
//     open("console", O_RDWR);
//   }
//   dup(0);  // stdout
//   dup(0);  // stderr

//   for(;;){
//     printf(1, "init: starting sh\n");
//     pid = fork();
//     if(pid < 0){
//       printf(1, "init: fork failed\n");
//       exit();
//     }
//     if(pid == 0){
//       exec("sh", argv);
//       printf(1, "init: exec sh failed\n");
//       exit();
//     }
//     while((wpid=wait()) >= 0 && wpid != pid)
//       printf(1, "zombie!\n");
//   }
// }

// init: The initial user-level program with login system

// init: The initial user-level program with login system

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define MAX_ATTEMPTS 3
#define STR_LEN 32

void trim_newline(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            str[i] = '\0';
            break;
        }
    }
}

void login() {
    char username[STR_LEN];
    char password[STR_LEN];
    int attempts = 0;

    while (attempts < MAX_ATTEMPTS) {
        printf(1, "Enter Username: ");
        gets(username, STR_LEN);
        trim_newline(username);

        if (strcmp(username, USERNAME) != 0) {
            printf(1, "Invalid Username. Try again.\n");
            attempts++;
            continue;
        }

        printf(1, "Enter Password: ");
        gets(password, STR_LEN);
        trim_newline(password);

        if (strcmp(password, PASSWORD) == 0) {
            printf(1, "Login successful\n");
            return;
        } else {
            printf(1, "Incorrect Password. Try again.\n");
            attempts++;
        }
    }

    printf(1, "Too many failed attempts. System locked.\n");
    exit();
}

int main(void) {
    if (open("console", O_RDWR) < 0) {
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    dup(0);  // stdout
    dup(0);  // stderr

    // Call login before starting the shell
    login();

    for (;;) {
        printf(1, "init: starting sh\n");
        int pid = fork();
        if (pid < 0) {
            printf(1, "init: fork failed\n");
            exit();
        }
        if (pid == 0) {
            exec("sh", (char *[]){"sh", 0});
            printf(1, "init: exec sh failed\n");
            exit();
        }
        int wpid;
        while ((wpid = wait()) >= 0 && wpid != pid)
            printf(1, "zombie!\n");
    }
}
