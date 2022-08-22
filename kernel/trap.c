#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "sbi.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];
extern uint32_t uart0_irq;
extern uint32_t virtio0_irq;


// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // device interrupt treated in devintr()
  } else {
    // exception
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());

    // Print scause
    if(scause & 0x8000000000000000L){
      printf("Interruption cause: ");
      if(scause == 0x8000000000000001L){
        printf("Supervisor software interrupt\n");
      }else if(scause == 0x8000000000000005L){
        printf("Supervisor timer interrupt\n");
      }else if(scause == 0x8000000000000009L){
        printf("Supervisor external interrupt\n");
      }else if(scause >= 0x8000000000000010L){
        printf("Reserved for platform use\n");
      }
      else{
        printf("Reserved\n");
      }
    }else{
      printf("Exception cause: ");
      if(scause == 0){
        printf("Instruction address misaligned\n");
      }else if(scause == 1){
        printf("Instruction access fault\n");
      }else if(scause == 2){
        printf("Illegal instruction\n");
      }else if(scause == 3){
        printf("Breakpoint\n");
      }else if(scause == 4){
        printf("Load address misaligned\n");
      }else if(scause == 5){
        printf("Load access fault\n");
      }else if(scause == 6){
        printf("Store/AMO address misaligned\n");
      }else if(scause == 7){
        printf("Store AMO address fault\n");
      }else if(scause == 8){
        printf("Environment call from U-mode\n");
      }else if(scause == 9){
        printf("Environment call from S-mode\n");
      }else if(scause == 12){
        printf("Instruction page fault\n");
      }else if(scause == 13){
        printf("Load page fault\n");
      }else if(scause == 15){
        printf("Store/AMO page fault\n");
      }else if((scause >= 24 && scause <= 31) || (scause >= 48 && scause <= 63)){
        printf("Custom exception\n");
      }else{
        printf("Reserved\n");
      }
    }

    printf(" (%d)", cpuid());

    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}


// Schedule timer interrupt.
// It is caught by devintr which schedules the next timer interrupt infinitely.
void
timerinit()
{
  uint64 now = r_time();
  uint64 date = now + SCHED_INTERVAL;

  sbi_set_timer(date);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == uart0_irq){
      uartintr();
    } else if(irq == virtio0_irq){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    w_sip(r_sip() & ~SIP_SEIP);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // supervisor software interrupt
    panic("supervisor software interrupt");
  } else if(scause == 0x8000000000000005L){
    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the interrupt by clearing the STIP bit in sip.
    timerinit();
    w_sip(r_sip() & ~SIP_STIP);

    return 2;
  } else {
    return 0;
  }
}

