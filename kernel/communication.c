#include <stdatomic.h>
#include "communication.h"
#include "sbi.h"
#include "topology.h"
#include "riscv.h"
#include "defs.h"

static struct ring* mybuf;
char comm_ready;

void
process_msg()
{
  size_t icons = atomic_load(&mybuf->icons);
  void (*func)(uintptr_t a1, uintptr_t a2);

 restart:
  func = atomic_load(&mybuf->messages[icons].func);

  if(func) {
    func(mybuf->messages[icons].a1, mybuf->messages[icons].a2);
    atomic_store(&mybuf->messages[icons].func, NULL);
    icons = (icons+1) % NMESSAGES;
    goto restart;
  }

  atomic_store(&mybuf->icons, icons);
}


void
send(int dest, void (*func)(uintptr_t a1, uintptr_t a2), uintptr_t a1, uintptr_t a2)
{
  struct domain* d = get_domain(dest);
  struct ring* buf = d->combuf;

  size_t iprod, iprodinc;

  push_off(); // avoid interrupt (scheduling) in this code

restart:
  iprod = atomic_load(&buf->iprod);
  iprodinc = (iprod+1) % NMESSAGES;

  if(iprodinc == buf->icons){
    // TODO: add a pause() here
    goto restart;
  }

  if(!atomic_compare_exchange_strong(&buf->iprod, &iprod, iprodinc))
    goto restart;

  buf->messages[iprod].a1 = a1;
  buf->messages[iprod].a2 = a2;
  atomic_store(&buf->messages[iprod].func, func);

  pop_off(); // now, can schedule again
  
  // TODO: enable more than sizeof(ulong) core on the machine
  unsigned long hart_mask = 1 << d->cpus->lapic; // Case of dom 0 where cpu 0 is not the first in the list
  // unsigned long hart_mask = 1;
  sbi_send_ipi(&hart_mask);
}


void
initcomm()
{
  mybuf = ((struct domain*)my_domain())->combuf;
  mybuf->icons = 0;
  mybuf->iprod = 0;
  comm_ready = 1;
}

void
remote_printf(uintptr_t a1, uintptr_t a2)
{
  // printf("(RPC: %d -> %d) ", a2, cpuid());
  printf("%s", a1);
}
