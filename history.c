#include "types.h"
#include "user.h"
#include "syscall.h"

int
main(int argc, char *argv[])
{
  // Allocate maximum possible buffer (can be adjusted)
  struct ProcessHistory history[256];
  
  int count = gethistory(history);
  
  if(count < 0) {
    printf(1, "No process history available\n");
    exit();
  }
  
  printf(1, "PID\tNAME\t\tTOTAL MEMORY\n");
  for(int i = 0; i < count; i++) {
    printf(1, "%d\t%s\t\t%d\n", 
           history[i].pid, 
           history[i].name, 
           history[i].total_memory);
  }
  
  exit();
}