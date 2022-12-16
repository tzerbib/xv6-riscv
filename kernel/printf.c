//
// formatted console output -- printf, panic.
//

#include <stdarg.h>
#include <stdint.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "topology.h"
#include "communication.h"

volatile int panicked = 0;
extern struct device* uart0;

// lock to avoid interleaving concurrent printf's.
static struct {
  struct spinlock lock;
  int locking;
} pr;

static char digits[] = "0123456789abcdef";
static char msg_buf[PGSIZE];


static void
add_buff(char c)
{
  static int i = 0;
  msg_buf[i++] = c;
  if(c == '\0')
    i = 0;
}

void
print_buf()
{
  int i = 0;
  while(msg_buf[i] != '\0')
    consputc(msg_buf[i++]);
}

static void
printint(int xx, int base, int sign)
{
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    add_buff(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;
  add_buff('0');
  add_buff('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    add_buff(digits[x >> (sizeof(uint64) * 8 - 4)]);
}


// Print to the console. only understands %d, %x, %p, %s.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c, locking;
  char *s;

  push_off(); // TODEL
  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  if (fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      add_buff(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'c':
      add_buff(va_arg(ap, int));
      break;
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        add_buff(*s);
      break;
    case '%':
      add_buff('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      add_buff('%');
      add_buff(c);
      break;
    }
  }
  add_buff('\0');

  // Remote printf
  if(pr.locking && comm_ready && uart0->owner != my_domain()){
    char* msg = kalloc();
    int i = 0;
    while(msg_buf[i] != '\0'){
      msg[i] = msg_buf[i];
      i++;
    }
    msg[i] = '\0';
    send(uart0->owner->domain_id, remote_printf, (uintptr_t)msg, cpuid());
  }else
    print_buf();

  if(locking)
    release(&pr.lock);
  pop_off(); // TODEL
}

void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");
  panicked = 1; // freeze uart output from other CPUs
  for(;;);
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}
