// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

#define NUM_CONSOLES 4
#define TERM_NUM_ROWS 25
#define TERM_NUM_COLS 80
#define INPUT_BUF 128

struct termbuf {
  char b[TERM_NUM_COLS][TERM_NUM_ROWS];
  int x;
  int y;
  // as new lines come in on the bottom, we treat this like a ring buffer
  int top_y;
  int rows_used;
};

struct input {
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
};

struct input inputs[NUM_CONSOLES];
int current_console = 0;


struct termbuf termbufs[NUM_CONSOLES];
struct termbuf last_out;

static void consputc(int, int);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i], current_console);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c, current_console);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s, current_console);
      break;
    case '%':
      consputc('%', current_console);
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%', current_console);
      consputc(c, current_console);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    if(pos > 0) --pos;
  } else
    crt[pos++] = (c&0xff) | 0x0700;  // black on white

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");

  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  crt[pos] = ' ' | 0x0700;
}

void
termbufputc(int c, int console_id){
  struct termbuf *tb = &termbufs[console_id];
  int incr_col = 0;

  if(c == BACKSPACE){
    // only backspace on the current line
    if(tb->x != 0){
      tb->x -= 1;
      tb->b[tb->x][tb->y] = ' ';
    }
  } else if(c == '\n'){
    incr_col = 1;
    tb->x = 0;
  } else if(c == '\r'){
    //incr_col = 1;
    //tb->x = 0;
  } else {
    tb->b[tb->x][tb->y] = c;
    tb->x += 1;
    if(tb->x >= TERM_NUM_COLS){
      tb->x = 0;
      incr_col = 1;
    }
  }
  if(incr_col){
    tb->y = (tb->y + 1) % TERM_NUM_ROWS;
    for(int i=0; i<TERM_NUM_COLS; ++i){
      tb->b[i][tb->y] = ' ';
    }
    if(tb->rows_used >= TERM_NUM_ROWS){
      tb->top_y = (tb->top_y + 1) % TERM_NUM_ROWS;
    } else {
      tb->rows_used += 1;
    }
  }
}

void uart_clear(){
  uartputc(0x1b);
  uartputc('[');
  uartputc('2');
  uartputc('J');
}
void uart_goto_xy(int x, int y){
  // terminal locations are 1-based...
  ++y; ++x;
  uartputc(0x1b);
  uartputc('[');
  // TODO -- deal with >100
  uartputc(y/10 + '0');
  uartputc(y%10 + '0');
  uartputc(';');
  uartputc(x/10 + '0');
  uartputc(x%10 + '0');
  uartputc('H');
}
void uart_putc_xy(int x, int y, char c){
  uart_goto_xy(x,y);
  uartputc(c);
}

void
term_uart_print(int console_id){
  struct termbuf *tb = &termbufs[console_id];
  for(int y=0; y<TERM_NUM_ROWS; ++y){
    int ey = (y+tb->top_y) % TERM_NUM_ROWS;
    for(int x=0; x<TERM_NUM_COLS; ++x){
      char newc = tb->b[x][ey];
      char oldc = last_out.b[x][y];
      if(newc != oldc){
        last_out.b[x][y] = newc;
        uart_putc_xy(x, y, newc);
      }
    }
  }
}

void
consputc(int c, int console_id)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  // be sure to clean things up before we start writing stuff
  static int firstput = 1;
  if(firstput){
    uart_clear();
    uart_goto_xy(0,0);
    firstput = 0;
  }

  termbufputc(c, console_id);
//  if(c == BACKSPACE){
//    uartputc('\b'); uartputc(' '); uartputc('\b');
//  } else
//    uartputc(c);
  term_uart_print(console_id);
  cgaputc(c);
}

#define C(x)  ((x)-'@')  // Control-x

void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;
  struct input *input = &inputs[current_console];

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'):  // Kill line.
      while(input->e != input->w &&
            input->buf[(input->e-1) % INPUT_BUF] != '\n'){
        input->e--;
        consputc(BACKSPACE, current_console);
      }
      break;
    case C('H'): case '\x7f':  // Backspace
      if(input->e != input->w){
        input->e--;
        consputc(BACKSPACE, current_console);
      }
      break;
    default:
      if(c != 0 && input->e-input->r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input->buf[input->e++ % INPUT_BUF] = c;
        consputc(c, current_console);
        if(c == '\n' || c == C('D') || input->e == input->r+INPUT_BUF){
          input->w = input->e;
          wakeup(&input->r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();  // now call procdump() wo. cons.lock held
  }
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;
  struct input *input = &inputs[current_console];

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while(n > 0){
    while(input->r == input->w){
      if(myproc()->killed){
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input->r, &cons.lock);
    }
    c = input->buf[input->r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input->r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff, current_console);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");

  for(int i=0; i<NUM_CONSOLES; ++i){
    memset(&(termbufs[i].b), ' ', TERM_NUM_ROWS*TERM_NUM_COLS);
    termbufs[i].x = 0;
    termbufs[i].y = 0;
    termbufs[i].top_y = 0;
    termbufs[i].rows_used = 1;

    memset(&(inputs[i].buf), 0, INPUT_BUF);
    inputs[i].r = 0;
    inputs[i].w = 0;
    inputs[i].e = 0;
  }
  memset(&(last_out.b), ' ', TERM_NUM_ROWS*TERM_NUM_COLS);

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  uart_clear();
  ioapicenable(IRQ_KBD, 0);
}

