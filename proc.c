#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  p->cpu_ticks = 0;
  p->wait_time = 0;
  p->creation_time = ticks;  // Track process creation time
  p->cs = 0;  // Initialize context switch count
  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  np->exec_time=-1;
  np->start_later=0;
  // np->creation_time = ticks;  // Track process creation time
  // np->cs = 0;  // Initialize context switch count
  np->first_scheduled = 0;
  

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
  curproc->end_time = ticks; // Record process completion time
  // cprintf("End time: %d", curproc->end_time);
  // cprintf("Start time: %d", curproc->creation_time);
  curproc->tat = curproc->end_time - curproc->creation_time;
  curproc->wt = curproc->tat - curproc->cpu_ticks;  // WT = TAT - CPU execution time
  // cprintf("switches: %d\n",curproc->switches);
  
  cprintf("PID: %d\n", curproc->pid);
  cprintf("TAT: %d\n", curproc->tat);
  cprintf("WT: %d\n", curproc->wt);
  cprintf("RT: %d\n", curproc->rt);
  cprintf("#CS: %d\n", curproc->cs);
  
  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  


  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

// int wait(void) {
//   struct proc *p;
//   int havekids, pid;
//   struct proc *curproc = myproc();

//   acquire(&ptable.lock);
//   for (;;) {
//       havekids = 0;
//       for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//           if (p->parent != curproc)
//               continue;
//           havekids = 1;
//           if (p->state == ZOMBIE) {
//               pid = p->pid;
//               // Clean up the process
//               kfree(p->kstack);
//               p->kstack = 0;
//               freevm(p->pgdir);
//               p->state = UNUSED;
//               p->pid = 0;
//               p->parent = 0;
//               p->name[0] = 0;
//               release(&ptable.lock);
//               return pid;
//           }
//       }
//       if (!havekids || curproc->killed) {
//           release(&ptable.lock);
//           return -1;
//       }
//       // sleep(curproc, &ptable.lock); // Wait for child process to finish
//       release(&ptable.lock);  // ✅ Release lock before sleeping
//       sleep(curproc, &ptable.lock);
//       acquire(&ptable.lock);  // ✅ Reacquire after waking up

//   }
// }


//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

// void
// scheduler(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();
//   c->proc = 0;
  
//   for(;;){
//     // Enable interrupts on this processor.
//     sti();

//     // Loop over process table looking for process to run.
//     acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;

//       // Switch to chosen process.  It is the process's job
//       // to release ptable.lock and then reacquire it
//       // before jumping back to us.
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;

//       swtch(&(c->scheduler), p->context);
//       switchkvm();

//       // Process is done running for now.
//       // It should have changed its p->state before coming back.
//       c->proc = 0;
//     }
//     release(&ptable.lock);

//   }
// }

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.

// void scheduler(void) {
//   struct proc *p, *best;
//   struct cpu *c = mycpu();
//   c->proc = 0;

//   for (;;) {
//       sti(); // Enable interrupts on this processor

//       acquire(&ptable.lock);

//       best = 0;

//       // Loop over the process table to find the highest-priority RUNNABLE process.
//       for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//           if (p->state != RUNNABLE)
//               continue;
//           cprintf("considered this one");
//           cprintf("pid in loop: %d\n", p->pid);
//           // Increase wait time for all RUNNABLE processes.
//           p->wait_time++;

//           // Compute dynamic priority based on CPU usage and wait time.
//           p->priority = INIT_PRIORITY - ALPHA * p->cpu_ticks + BETA * p->wait_time;

//           // Select the process with the highest priority.
//           if (best == 0 || p->priority > best->priority || 
//               (p->priority == best->priority && p->pid < best->pid)) {
//               best = p;
//           }
//       }

//       if (best) {
//         cprintf("the best process below\n");
//         cprintf("PID: %d\n", best->pid);
//           best->state = RUNNING;
//           best->cpu_ticks++;  // Increment CPU usage time
//           best->wait_time = 0; // Reset wait time since it's running now

//           // If the process has a time limit and has used up its execution time, terminate it.
//           if (best->exec_time != -1 && best->cpu_ticks >= best->exec_time) {
//               best->state = ZOMBIE; // Mark for cleanup
//           } else {
//               // Context switch to the chosen process
//               c->proc = best;
//               switchuvm(best);
//               swtch(&(c->scheduler), best->context);
//               switchkvm();
//               c->proc = 0; // Reset CPU process pointer
//           }
//       }

//       release(&ptable.lock);
//   }
// }

//BEST TILL NOW
void
scheduler(void){
  struct proc *p;
  struct proc *p1;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    struct proc *highP =  0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      // p->wait_time++;
      p->wait_time=ticks-p->creation_time-p->cpu_ticks;
      p->priority = INIT_PRIORITY - ALPHA * p->cpu_ticks + BETA * p->wait_time;
      
      highP = p;
      // Choose one with highest priority
      for(p1=ptable.proc; p1<&ptable.proc[NPROC];p1++){
          if(p1->state != RUNNABLE)
            continue;
          
          // p1->wait_time++;
          p1->wait_time=ticks-p1->creation_time-p1->cpu_ticks;
          p1->priority = INIT_PRIORITY - ALPHA * p1->cpu_ticks + BETA * p1->wait_time;
          if(highP->priority < p1->priority||(highP->priority==p1->priority && highP->pid>p1->pid)) // larger value, lower priority
            highP = p1;
      }
      p=highP;
      if (p->first_scheduled == 0) {
        p->rt = ticks - highP->creation_time;  // Response time = first execution - creation
        p->first_scheduled = 1;
      }
      p->cs++;  // Count context switches
      // highP->cpu_ticks++;  // Increment CPU usage time
      // p = highP;
      p->wait_time = 0;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      // int start=ticks;
      swtch(&(c->scheduler), p->context);
      // int end=ticks;
      // p->cpu_ticks+=(end-start);
      switchkvm();

      c->proc = 0;
      break;
    }
    // highP->cs++;  // Count context switches
    // highP->cpu_ticks++;  // Increment CPU usage time
    // highP->wait_time = 0;
    release(&ptable.lock);

  }
}

//SEE
// void
// scheduler(void){
//   struct proc *p;
//   struct cpu *c = mycpu();
//   c->proc = 0;
//   for(;;){
//     // Enable interrupts on this processor.
//     sti();
//     // Loop over process able looking for process to run.
//     acquire(&ptable.lock);
//     struct proc *highP =  0;
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;
//       // p->wait_time++;
//       p->wait_time=ticks-p->creation_time-p->cpu_ticks;
//       p->priority = INIT_PRIORITY - ALPHA * p->cpu_ticks + BETA * p->wait_time;
//       if(highP->priority < p->priority||(highP->priority==p->priority && highP->pid>p->pid)) // larger value, lower priority
//             highP = p;
//     }
//     p = highP;
//       if (p->first_scheduled == 0) {
//         p->rt = ticks - p->creation_time;  // Response time = first execution - creation
//         p->first_scheduled = 1;
//       }
//       // p = highP;
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;
//       // int start=ticks;
//       swtch(&(c->scheduler), p->context);
//       // int end=ticks;
//       // p->cpu_ticks+=(end-start);
//       p->cs++;
//       // p->wait_time=0;
//       switchkvm();
//       c->proc = 0;
//     }
//     release(&ptable.lock);
// }

// void
// scheduler(void)
// {
//   struct cpu *c = mycpu();
//   struct proc *p;
//   c->proc = 0;

//   for(;;){
//     sti();  // Enable interrupts on this processor

//     acquire(&ptable.lock);

//     struct proc *selected_proc = 0;

//     // Loop to pick the process with highest dynamic priority
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;

//       // Update waiting time
//       // p->waiting_time = ticks - p->last_scheduled;

//       // if (p->last_scheduled == 0)
//       //   p->waiting_time = ticks - p->arrival_time;
//       // else
//       //   p->waiting_time = ticks - p->last_scheduled;


//       int waiting_time = ticks - p->creation_time - p->cpu_ticks;
//       if(waiting_time<0)waiting_time=0;

//       // Compute dynamic priority using formula:
//       // πi(t) = πi(0) - α·C(t) + β·W(t)
//       p->priority = INIT_PRIORITY - (ALPHA * p->cpu_ticks) + (BETA * waiting_time);

//       // cprintf("PID %d: priority = %d, waiting = %d, cpu = %d\n", p->pid, p->priority, waiting_time, p->cpu_ticks);


//       // Select process with highest priority (tie breaker: lower PID)
//       if (!selected_proc) {
//         selected_proc = p;
//       } else if (p->priority > selected_proc->priority) {
//         selected_proc = p;
//       } else if (p->priority == selected_proc->priority && p->pid < selected_proc->pid) {
//         selected_proc = p;
//       }
//     }


//     if (selected_proc) {
//       p = selected_proc;

//       // p->waiting_time = 0;              // reset wait time
//       // p->last_scheduled = ticks;        // mark last scheduled time

//       // Set response time if first scheduled
//       if (p->rt == 0)
//         p->rt = ticks - p->creation_time;

//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;

//       p->cs++;

//       swtch(&c->scheduler, p->context);


//       switchkvm();

//       // if (p->exec_time > 0) {
//       //   p->exec_time -= (end - start);
//       //   if (p->exec_time <= 0) {
//       //     p->killed = 1;
//       //   }
//       // }

//       c->proc = 0;
//     }

//     release(&ptable.lock);
//   }
// }

// void
// scheduler(void){
//   struct proc *p;
//   // struct proc *p1;
//   struct cpu *c = mycpu();
//   c->proc = 0;

//   for(;;){
//     // Enable interrupts on this processor.
//     sti();

//     struct proc *highP =  0;
//     // Loop over process table looking for process to run.
//     acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;
//       // p->wait_time++;
      
//       p->priority = INIT_PRIORITY - ALPHA * p->cpu_ticks + BETA * p->wait_time;
//       if(highP==0){
//         highP=p;
//       }
//       else{
//         if(highP->priority < p->priority||(highP->priority==p->priority && highP->pid>p->pid)) // larger value, lower priority
//             highP = p;
//       }
//       // Choose one with highest priority
//       if (highP->first_scheduled == 0) {
//         highP->rt = ticks - highP->creation_time;  // Response time = first execution - creation
//         highP->first_scheduled = 1;
//       }
//       highP->cs++;  // Count context switches
//       highP->cpu_ticks++;  // Increment CPU usage time
//       p->wait_time = 0;
//       p = highP;
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;
      
//       swtch(&(c->scheduler), p->context);
//       switchkvm();

//       c->proc = 0;
//     }
//     // highP->cs++;  // Count context switches
//     // highP->cpu_ticks++;  // Increment CPU usage time
//     // highP->wait_time = 0;
//     release(&ptable.lock);

//   }
// }


void
scheduler_start(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      // For all processes that are delayed and sleeping
      if(p->state == SLEEPING && p->start_later) {
          p->state = RUNNABLE;
          p->start_later = 0;  // Clear the flag once activated.
      }
  }
  release(&ptable.lock);
}

int custom_fork(int start_later, int exec_time) {
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
      return -1;
  }

  // Copy process state from parent.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {
      kfree(np->kstack);
      np->kstack = 0;
      np->state = UNUSED;
      return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
      if (curproc->ofile[i])
          np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  // Assign start_later and exec_time
  np->start_later = start_later;
  np->exec_time = exec_time;
  // curproc->cpu_ticks = 0;
  // curproc->wait_time = 0;
  // np->creation_time = ticks;  // Track process creation time
  // np->cs = 0;  // Initialize context switch count
  curproc->priority = INIT_PRIORITY;
  
  np->first_scheduled = 0;
  

  pid = np->pid;

  acquire(&ptable.lock);

  // If start_later is set, keep it in SLEEPING state until sys_scheduler_start() is called
  if (start_later)
      np->state = SLEEPING;
  else
      np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  p->switches++;
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
