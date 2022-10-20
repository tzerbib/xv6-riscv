#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "memlayout.h"

extern char trampoline[], vvec[], vret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

uint64 von(void) {
  // set S Previous Privilege mode to Supervisor (will be VS actually, with V set in vtrapret).
  unsigned long x = r_sstatus();
  x |= SSTATUS_SPP;
  x |= SSTATUS_SPIE;
  w_sstatus(x);

  return 0;
}

void vtrap(void) {
  if((r_hstatus() & HSTATUS_SPV) == 0)
    panic("vtrap: not from virtual mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();

  // an interrupt will change sstatus &c registers,
  // so don't enable until done with those registers.
  intr_on();

  printf("vtrap(): scause %p pid=%d\n", r_scause(), p->pid);
  printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
  printf("            htval=%p\n", r_htval());
  p->killed = 1;

  if(p->killed)
    exit(-1);

  vtrapret();
}

void vtrapret(void) {
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (vvec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)vtrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to VS.
  unsigned long x = r_hstatus();
  // Enable V mode
  x |= HSTATUS_SPV;
  w_hstatus(x);
  // Enable HS interrupts
  x = r_sstatus();
  x |= SSTATUS_SPIE;
  w_sstatus(x);

  printf("hstatus = %p\n", r_hstatus());
  printf("sstatus = %p\n", r_sstatus());

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 hgatp = MAKE_HGATP(p->pagetable);
  /*printf("hgatp = %p\n", hgatp);*/
  /*printf("hedeleg = %p ; hideleg = %p\n", r_hedeleg(), r_hideleg());*/
  /*printf("hgeip = %p ; hgeie = %p\n", r_hgeip(), r_hgeie());*/

  // jump to trampoline.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (vret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, hgatp);
}
