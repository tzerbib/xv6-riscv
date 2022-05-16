#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "topology.h"


volatile static int started = 0;
volatile static int my_test = 0;

extern void _entry(void);
char* p_entry;         // first physical address of kernel.
extern char end[];            // first address after kernel.

// keep each CPU's hartid in its tp register, for cpuid().
// Initially done in start (start.c)
static inline void inithartid(unsigned long hartid){
  w_tp(hartid);
}


static void kexec(void* domain, void* args){
  struct domain* d = domain;
  struct memrange *mr;


  // Avoid domain from main boot hart
  if(my_domain() == d) return;

  ptr_t ksize = end - (char*)_entry;

  // Get an arbitrary memory range large enough to place the kernel
  for(mr=d->memranges; mr && mr->length < ksize && mr->reserved; mr=mr->next);

  if(!mr){
    printf(
      "kexec: domain %d has not enought memory for the kernel (max %d/%d)\n",
      d->domain_id, mr->length, ksize
    );
    panic("");
  }

  // Copy kernel text data and BSS
  memmove(mr->start, _entry, ksize);

  // Wake up an arbitrary hart of the domain
  sbi_start_hart(d->cpus->lapic, (unsigned long)mr->start, 0);
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

    kinit();           // physical page allocator
    kvminit();         // create kernel page table


    printf("\n");
    init_topology();
    add_numa();
    finalize_topology();
    assign_freepages((void*) dtb_pa);
    printf("\n\n--- Computed topology: ---\n\n");
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
    forall_domain(kexec, 0);
    my_test = 42;

    __sync_synchronize();
    started = 1;
  } else {
    printf("Hart %d started\n", cpuid());
    __sync_synchronize();
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  printf("Hart %d ready\n", cpuid());
  printf("my_test is %d\n", my_test);

  scheduler();
}
