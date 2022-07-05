#include "types.h"
#include "riscv.h"

extern void master_main(void);
extern void slave_main(void);

void
common_start(uint64 hartid)
{
  // Keep the hart ID in the tp register, for cpuid()
  w_tp(hartid);

  // Enable interrupts to supervisor level (external, timer, software)
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
}

void
master_start(uint64 hartid)
{
  common_start(hartid);

  master_main();
}

void
slave_start(uint64 hartid)
{
  common_start(hartid);

  slave_main();
}
