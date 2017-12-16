// Shell.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "param.h"

// Parsed command representation
#define EXEC    1
#define REDIR   2
#define PIPE    3
#define LIST    4
#define BACK    5
#define REDIRFD 6

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct redirfdcmd {
  int type;
  struct cmd *cmd;
  int outfd;
  int infd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
//  int fds[3];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  struct redirfdcmd *fcmd;

  if(cmd == 0)
    exit();

  switch(cmd->type){
    default:
      panic("runcmd");

    case EXEC:
      ecmd = (struct execcmd*)cmd;
      if(ecmd->argv[0] == 0)
        exit();
      exec(ecmd->argv[0], ecmd->argv);
      printf(2, "exec %s failed\n", ecmd->argv[0]);
      break;

    case REDIR:
      rcmd = (struct redircmd*)cmd;
      close(rcmd->fd);
      if(open(rcmd->file, rcmd->mode) < 0){
        printf(2, "open %s failed\n", rcmd->file);
        exit();
      }
      runcmd(rcmd->cmd);
      break;

    case REDIRFD:
      fcmd = (struct redirfdcmd*)cmd;
      close(fcmd->infd);
      dup(fcmd->outfd);
      runcmd(fcmd->cmd);
      break;

    case LIST:
      lcmd = (struct listcmd*)cmd;
      if(fork1() == 0)
        runcmd(lcmd->left);
      wait();
      runcmd(lcmd->right);
      break;

    case PIPE:
      pcmd = (struct pipecmd*)cmd;
      if(pipe(p) < 0)
        panic("pipe");
      if(fork1() == 0){
        close(1);
        dup(p[1]);
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->left);
      }
      if(fork1() == 0){
        close(0);
        dup(p[0]);
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->right);
      }
      close(p[0]);
      close(p[1]);
      wait();
      wait();
      break;

    case BACK:
      bcmd = (struct backcmd*)cmd;
      if(fork1() == 0)
        runcmd(bcmd->cmd);
      break;
  }
  exit();
}

int
getcmd(char *buf, int nbuf, int cons)
{
  printf(2, "%d> ", cons);
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int
main(int argc, char* argv[])
{
  static char buf[100];

  if (argc != 2) {
    panic("incorrect number of arguments to sh\n");
  }
  int cons_no = argv[1][0] - 48;

  // Close any existing low-level file descriptors. (These are leftovers from init.)
  close(0);
  close(1);
  close(2);
  // Open the appropriate console device. These should have been opened by init.
  dup(cons_no + 3);
  dup(cons_no + 3);
  dup(cons_no + 3);

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf), cons_no) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        printf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    else if(buf[0] == 's' && buf[1] == 'h' && (buf[2] == '\n' || buf[2] == ' ')){
      // Insert correct console device number.
      // TODO: This will break some things.
      buf[2] = ' ';
      buf[3] = cons_no + '0';
      buf[4] = '\n';
      buf[5] = 0;
      continue;
    }
    if(fork1() == 0) {
      runcmd(parsecmd(buf));
    }
    wait();
  }
  exit();
}

void
panic(char *s)
{
  printf(2, "%s\n", s);
  exit();
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
redirfdcmd(struct cmd *subcmd, int outfd, int infd)
{
  struct redirfdcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIRFD;
  cmd->cmd = subcmd;
  cmd->outfd = outfd;  // The fd which will be written to.
  cmd->infd = infd;  // The fd which will be closed for outfd to dup into.
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  // **ps   pointer to string
  // *es    end of string
  // **q    ...
  // **eq   ...
  char *s;  // start of string
  int ret;  // return value

  // Start at the head of the string. Iterate through all whitespace.
  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  // Set q to be the start of the token.
  if(q)
    *q = s;
  // Start with the beginning.
  ret = *s;
  switch(*s){
    case 0:
      break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
      s++;
      break;
    case '>':
      s++;
      if(*s == '>'){
        ret = '+';
        s++;
      } else if(*s == '&'){  // fd redirect
        ret = '*';
        s++;
      } else if(*s == 'c'){  // console redirect (just a pretty fd redirect)
        ret = 'c';
        s++;
      }
      break;
    default:
      // 'a' indicates that we have some non-special token.
      // Iterate forward to the end of the token.
      ret = 'a';
      while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
        s++;
      break;
  }
  // Set eq to the end of the parsed token.
  if(eq)
    *eq = s;

  // Iterate forward through any remaining whitespace.
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    printf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;
  int fd;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);         // Determine the specific type of redirect.
    if(gettoken(ps, es, &q, &eq) != 'a')  // Get the file argument.
      panic("missing file for redirection");
    switch(tok){
      case '<':
        cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
        break;
      case '>':
        cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
        break;
      case '+':  // >>
        cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
        break;
      case '*':  // >&  -- fd redirect
        fd = atoi(q);
        if (fd < 0 || fd >= 10)  // only support fds in [0, 9]
          panic("invalid redir fd");
        cmd = redirfdcmd(cmd, fd, 1);  // only redirects &1
        break;
      case 'c':  // >c  -- console redirect, which is an fd redirect with nicer numbers
        fd = atoi(q);
        if (fd < 0 || fd >= NCONS)  // only support fds for consoles
          panic("invalid console");
        fd += (NCONS - 1);  // init should have opened the appropriate fds for us
        cmd = redirfdcmd(cmd, fd, 1);  // only redirects &1
        break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  struct redirfdcmd *fcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
    case EXEC:
      ecmd = (struct execcmd*)cmd;
      for(i=0; ecmd->argv[i]; i++)
        *ecmd->eargv[i] = 0;
      break;

    case REDIR:
      rcmd = (struct redircmd*)cmd;
      nulterminate(rcmd->cmd);
      *rcmd->efile = 0;
      break;

    case REDIRFD:
      fcmd = (struct redirfdcmd*)cmd;
      nulterminate(fcmd->cmd);
      break;

    case PIPE:
      pcmd = (struct pipecmd*)cmd;
      nulterminate(pcmd->left);
      nulterminate(pcmd->right);
      break;

    case LIST:
      lcmd = (struct listcmd*)cmd;
      nulterminate(lcmd->left);
      nulterminate(lcmd->right);
      break;

    case BACK:
      bcmd = (struct backcmd*)cmd;
      nulterminate(bcmd->cmd);
      break;
  }
  return cmd;
}
