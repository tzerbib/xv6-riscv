#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "sbi.h"

void slave_main();

// start() jumps here in the boot hart (may not be hart 0).
void
main()
{
  consoleinit();
  printfinit();

  printf("\n");
  printf("xv6 kernel is configured for %d sockets and %d harts\n", NB_SOCKETS, NB_HARTS);
  printf("xv6 kernel is booting on hart %d\n", cpuid());
  printf("\n");

  kinit();         // physical page allocator
  kvminit();       // create kernel page table
  kvminithart();   // turn on paging
  timerinit();     // ask for clock interrupts.
  procinit();      // process table
  trapinit();      // trap vectors
  trapinithart();  // install kernel trap vector
  plicinit();      // set up interrupt controller
  plicinithart();  // ask PLIC for device interrupts
  binit();         // buffer cache
  iinit();         // inode table
  fileinit();      // file table
  virtio_disk_init(); // emulated hard disk
  userinit();      // first user process
  __sync_synchronize();

  for (int i = 0; i < NB_HARTS; i++) {
    if (i == cpuid())
      continue;

    sbi_start_hart(i, (uint64)slave_main, 0);
  }

  scheduler();
}

void slave_main() {
  __sync_synchronize();
  printf("hart %d starting\n", cpuid());
  kvminithart();    // turn on paging
  trapinithart();   // install kernel trap vector
  plicinithart();   // ask PLIC for device interrupts

  scheduler();
}
