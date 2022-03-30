#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

extern char __bss_start; // kernel.ld sets this to begin of BSS section
extern char __bss_end; // kernel.ld sets this to end of BSS section

extern void _entry(unsigned long hartid, unsigned long dtb_pa);

// keep each CPU's hartid in its tp register, for cpuid().
// Initially done in start (start.c)
static inline void inithartid(unsigned long hartid){
  w_tp(hartid);
}


// start() jumps here in supervisor mode on all CPUs.
void
main(unsigned long hartid, unsigned long dtb_pa)
{
  inithartid(hartid);

  // delegate all interrupts and exceptions to supervisor mode.
  // w_medeleg(0xffff);
  // w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  if(dtb_pa != 0){
    // initialize BSS section
    memset(&__bss_start, 0, (&__bss_end)-(&__bss_start));

    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is configured for %d sockets and %d harts\n", NB_SOCKETS, NB_HARTS);
    printf("xv6 kernel is booting on hart %d\n", cpuid());
    printf("\n");
    kinit((void*) dtb_pa);         // physical page allocator
    kvminit();       // create kernel page table


    printf("\n");
    // char* srat = init_SRAT();
    // print_srat(srat);
    init_topology();
    add_numa((void*) dtb_pa);
    // finalize_topology();
    // assign_freepages();
    // printf("\n\n--- Computed topology (old kalloc): ---\n\n");
    // print_topology();
    // print_struct_machine_loc();
    // printf("\n\n");

    // init_topology();
    // add_numa(srat);
    // finalize_topology();
    // assign_freepages();
    // free_machine();
    // printf("\n\n--- Computed topology (new kalloc): ---\n\n");
    // print_topology();
    // print_struct_machine_loc();
    // printf("\n\n");


    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    
    
    sbi_get_spec_version();
    
    
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process

    // Waik up all other cores by sending an ipi to them
    for(int i = 0; i < NB_HARTS; i++){
      if(i == hartid) continue;
      sbi_start_hart(i, (unsigned long)&_entry, 0);
    }

    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("Hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  printf("Hart %d booted\n", cpuid());

  scheduler();
}
