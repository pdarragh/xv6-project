// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "param.h"

int
main(void)
{
  int pid, wpid;
  int i, restart;
  int pids[NCONS] = {0};
  char* argv[3] = {"sh", "0", 0};
  char* consname = "console_x";  // x is at position 8.

  // Open console 0 to handle any output that needs to be done in init.
  if(open("console_0", O_RDWR) < 0){
    mknod("console_0", 1, 0);
    open("console_0", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  // Open extra fds for each console:
  for (i = 0; i < NCONS; ++i) {
    consname[8] = '0' + i;  // Set the appropriate console number.
    if(open(consname, O_RDWR) < 0){
      mknod(consname, 1, (short)i);
      open(consname, O_RDWR);
    }
  }

  int *j = malloc(sizeof(int));
  *j = 0;

  // Loop to constantly generate all four shells with appropriate console devices.
  // Downside: whenever a shell quits, all four consoles are checked... twice.
  // Upside: it works
  //         (also it allows a specific console number to be passed in, which a fork/dup method would not)
  for(;;){
    for (i = 0; i < NCONS; ++i) {
      if (pids[i] == 0) {
        // No process here yet!
        printf(1, "init: starting sh (%d)\n", i);
        pid = fork();
        if (pid < 0) {
          printf(1, "init: fork failed (%d)\n", i);
          exit();
        }
        // Create the shell for this console number.
        if (pid == 0) {
          // Pass in the console number and create new sh process.
          argv[1][0] += i;
          exec("sh", argv);
          printf(1, "init: exec sh failed (%d)\n", i);
          exit();
        } else {
          // Write down child process pid.
          pids[i] = pid;
        }
      }
    }

    // Determine whether we need to restart a process.
    restart = 0;
    while ((wpid = wait()) >= 0) {
      // We've waited for a process. Was it a shell we care about?
      for (i = 0; i < 4; ++i) {
        if (pids[i] == wpid) {
          // Yes. Clear the pid so we can start it again.
          pids[i] = 0;
          restart = 1;
        }
      }
      if (restart) {
        // Ugh fine restart.
        break;
      } else {
        // Unexpected process.
        printf(1, "zombie!\n");
      }
    }
  }
}
