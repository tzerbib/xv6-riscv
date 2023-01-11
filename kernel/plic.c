#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "topology.h"


extern struct device* uart0;
extern struct device* virtio0;
extern struct machine* machine;


//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
__plicinit(void* device, void* args)
{
  struct device* dev = device;

  // Ignore other devices than PLIC
  if(dev->id != ID_PLIC) return;
  *(uint32*)(dev->start + PLIC_PRIORITY_OFF + uart0->irq*PLIC_PRIORITY_SZ) = 1;
  *(uint32*)(dev->start + PLIC_PRIORITY_OFF + virtio0->irq*PLIC_PRIORITY_SZ) = 1;
}

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  forall_device(machine, __plicinit, 0);
}

void
plicinithart(void)
{
  int hart = cpuid();

  // set uart's enable bit for this hart's S-mode.
  *(uint32*)PLIC_SENABLE(hart) = (1 << uart0->irq) | (1 << virtio0->irq);

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
