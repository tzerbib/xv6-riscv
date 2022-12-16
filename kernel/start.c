#include "types.h"
#include "riscv.h"

// entry.S needs 2 stacks for booting hart 0 and giving him one.
__attribute__ ((aligned (16))) char stack0[PGSIZE];
__attribute__ ((aligned (16))) char stack1[PGSIZE];


extern void machine_master_main(void);
extern void domain_master_main(ptr_t);
extern void domain_master_wakeup(uint64_t);
extern void slave_main();


// keep each CPU's hartid in its tp register, for cpuid().
// Initially done in start (start.c)
static inline void inithartid(unsigned long hartid){
  w_tp(hartid);
}

static inline void
common_init(uint64_t hartid)
{
  inithartid(hartid); // Keep the hart ID

  // Enable interrupts to supervisor level (external, timer, software)
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
}

void
machine_master_start(uint64_t hartid)
{
  common_init(hartid);
  machine_master_main();
}

void
domain_master_start(uint64_t hartid, ptr_t args)
{
  common_init(hartid);
  domain_master_main(args);
}

void domain_master_wakeup_start(uint64_t hartid)
{
  common_init(hartid);
  domain_master_wakeup(hartid);
}

void
slave_start(uint64_t hartid)
{
  common_init(hartid);
  slave_main();
}