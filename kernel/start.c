#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// entry.S jumps here in machine mode on stack0.
void
start(uint64 hartid)
{
  // keep each CPU's hartid in its tp register, for cpuid().
  w_tp(hartid);

  // Enable interrupts to supervisor level (external, timer, software)
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  main();
}
