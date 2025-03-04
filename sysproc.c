#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "syscall.h"
#include "fs.h"
#include "fcntl.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_gethistory(void) {
  return gethistory();
}

// Block a syscall
// int sys_block(void) {
//   int syscall_id;
//   if (argint(0, &syscall_id) < 0)
//       return -1;
  
//   if (syscall_id == SYS_fork || syscall_id == SYS_exit)  // Cannot block critical syscalls
//       return -1;

//   blocked_syscalls[syscall_id] = 1;
//   return 0;
// }

// // Unblock a syscall
// int sys_unblock(void) {
//   int syscall_id;
//   if (argint(0, &syscall_id) < 0)
//       return -1;

//   blocked_syscalls[syscall_id] = 0;
//   return 0;
// }
int NSYSCALLS=25;
int sys_block(void) {
  int syscall_num;
  if (argint(0, &syscall_num) < 0)
    return -1;
  
  if (syscall_num < 0 || syscall_num >= NSYSCALLS)
    return -1;

  myproc()->blocked_syscalls_pending[syscall_num] = 1;  // Only update pending array
  return 0;
}

int sys_unblock(void) {
  int syscall_num;
  if (argint(0, &syscall_num) < 0)
    return -1;
  
  if (syscall_num < 0 || syscall_num >= NSYSCALLS)
    return -1;

  myproc()->blocked_syscalls_pending[syscall_num] = 0;  // Only update pending array
  myproc()->blocked_syscalls[syscall_num]=0;
  return 0;
}


