// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

// this is a duplicate, but I don't want to bother putting one in a common place right now.
#define NUM_CONSOLES 4

int
main(void)
{
  int pid, wpid;

  int i = 0;
  // just make 4 init processes that each spawn shells
  for(; i < NUM_CONSOLES - 1; ++i){
    pid = fork();
    if(pid == 0){
      break;
    }
  }

  char consname[10];
  memmove(consname, "console0", 9);
  consname[8] = i + '0';
  if(open(consname, O_RDWR) < 0){
    mknod(consname, 1, i);
    open(consname, O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
