#include "types.h"
 #include "stat.h"
 #include "user.h"
 int fib(int n) {
 if (n <= 0) return 0;
 if (n == 1) return 1;
 if (n == 2) return 1;
 return fib(n- 1) + fib(n- 2);
 }
 int main() {
 int pid = fork();
 if (pid != 0) {
 while (1) {
 printf(1, "Hello, I am parent\n");
 fib(35);
 }
 }
 else {
 while (1) {
 printf(1, "Hi there, I am child\n");
 fib(35);
 }
 }
 }