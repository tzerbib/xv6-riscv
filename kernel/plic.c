#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  int i;
  // set desired IRQ priorities non-zero (otherwise disabled).
  for (i = 0; i < NB_SOCKETS; i++) {
      *(uint32*)PLIC_PRIORITY(i, UART0_IRQ) = 1;
      *(uint32*)PLIC_PRIORITY(i, VIRTIO0_IRQ) = 1;
  }
}

void
plicinithart(void)
{
  int hart = cpuid();

  // set uart's enable bit for this hart's S-mode.
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIOTHRESH(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCOMPLETE(hart) = irq;
}
