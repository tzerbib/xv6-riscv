#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "topology.h"

volatile static int started = 0;

extern void _entry(void);
char* p_entry;         // first physical address of kernel.

// keep each CPU's hartid in its tp register, for cpuid().
// Initially done in start (start.c)
static inline void inithartid(unsigned long hartid){
  w_tp(hartid);
}


static inline void wakeup_cores(void* cpu_desc, void* args){
  struct cpu_desc* cpu = cpu_desc;
  uint32_t* boot_hart = args;

  if(cpu->lapic != *boot_hart)
    sbi_start_hart(cpu->lapic, (unsigned long)&_entry, 0);
}


// start() jumps here in supervisor mode on all CPUs.
void
main(unsigned long hartid, ptr_t dtb_pa, ptr_t p_kstart)
{
  inithartid(hartid);

  // delegate all interrupts and exceptions to supervisor mode.
  // w_medeleg(0xffff);
  // w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  if(dtb_pa != 0){
    p_entry = (void*) p_kstart;
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting on hart %d\n", cpuid());
    printf("\n");
    
    initialize_fdt((void*)dtb_pa);
    printf("Correct FDT at %p\n", dtb_pa);
    print_dtb();

    kinit();    // physical page allocator
    kvminit();         // create kernel page table


    printf("\n");
    init_topology();
    add_numa();
    finalize_topology();
    assign_freepages((void*) dtb_pa);
    printf("\n\n--- Computed topology (old kalloc): ---\n\n");
    print_topology();
    print_struct_machine_loc();
    printf("\n\n");

    init_topology();
    add_numa();
    finalize_topology();
    assign_freepages((void*) dtb_pa);
    free_machine();
    printf("\n\n--- Computed topology (new kalloc): ---\n\n");
    print_topology();
    print_struct_machine_loc();
    printf("\n\n");


    kvminithart();   // turn on paging
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


    // Wake up all other cores by sending an ipi to them
    forall_cpu(wakeup_cores, &hartid);


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
