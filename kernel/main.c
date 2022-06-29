#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "topology.h"
#include "kexec.h"


volatile static int my_test = 0;
// extern int my_test;

char* p_entry;
// extern char* p_entry;              // first physical address of kernel.

ptr_t ksize;
extern void _entry(void);
extern void _boot(void);
extern char end[];                 // first address after kernel.
extern struct machine* machine;    // Beginning of whole machine structure
extern char stack0[];


// keep each CPU's hartid in its tp register, for cpuid().
// Initially done in start (start.c)
static inline void inithartid(unsigned long hartid){
  w_tp(hartid);
}



// _entry jumps here in supervisor mode on CPU 0.
void
preboot(unsigned long hartid, ptr_t dtb_pa)
{
  inithartid(hartid);
  ksize = (ptr_t)end - (ptr_t)_entry;


  // delegate all interrupts and exceptions to supervisor mode.
  // w_medeleg(0xffff);
  // w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  consoleinit();
  printfinit();
  printf("\n");
  printf("xv6 kernel is booting\n");
  printf("\n");

  initialize_fdt((void*)dtb_pa);
  printf("Correct FDT at %p\n", dtb_pa);
  print_dtb();

  kinit();           // physical page allocator
  kvminit();         // create kernel page table

  printf("\n");
  init_topology(0);
  add_numa();
  finalize_topology();
  assign_freepages((void*) dtb_pa);
  printf("\n\n--- Computed topology: ---\n\n");
  print_topology();
  print_struct_machine_loc();
  printf("\n\n");

  // Ensure that the kernel text is in the local memory
  struct memrange* mr = find_memrange(machine, preboot);
  if(mr->domain != my_domain())
    panic("kernel text is on a distant memory range: unimplemented");

  // Add execute permission to the kernel text
  if(vmperm((pte_t)_entry, end - (char*)_entry, PTE_X, 1)){
    printf("kernel text for domain %d is not mapped\n", mr->domain);
    panic("");
  }


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

  __sync_synchronize();

  // Wake up all other cores by sending an ipi to them
  forall_domain(kexec, (void*)dtb_pa);

  my_test = 42;
  // while(my_test==42){
  //   // printf("my_test: %d (%p)\n", my_test, &my_test);
  // }
  // for(int i=0; i<9999;i++);
  // printf("NOOOOOOOOOOOOOOOOO\n");
  // for(;;);

  printf("Hart %d: my_test is %d\n", cpuid(), my_test);

  scheduler();
}


// _entry jumps here in supervisor mode on all domain master CPUs.
void
boot(unsigned long hartid)
{
  inithartid(hartid);
  printf("Hart %d: my_test is %d\n", cpuid(), my_test);

  // printf("slave: my_test: %d (%p)\n", my_test, &my_test);
  // my_test = 0;
  // printf("my_test is now 0\n");
  // for(;;);

  p_entry = (char*)((ptr_t)p_entry + _entry - _boot);
  struct boot_arg* tmp_bargs = (void*)PGROUNDUP((ptr_t)p_entry+ksize);
  struct boot_arg bargs;
  bargs.dtb_pa = tmp_bargs->dtb_pa;
  bargs.current_domain = tmp_bargs->current_domain;
  p_entry = (char*)tmp_bargs->p_entry;

  initialize_fdt((void*)bargs.dtb_pa);
  kinit();           // physical page allocator
  kvminit();         // create kernel page table

  printf("\n");
  init_topology(bargs.current_domain);
  add_numa();
  finalize_topology();
  assign_freepages((void*)bargs.dtb_pa);
  printf("\n\n--- Computed topology: ---\n\n");
  print_topology();
  print_struct_machine_loc();
  printf("\n\n");

  // Add execute permission to the kernel text
  if(vmperm((pte_t)p_entry, ksize, PTE_X, 1)){
    printf("kernel text for domain %d is not mapped\n",
      ((struct memrange*)find_memrange(machine, boot))->domain);
    panic("");
  }

  // kvminithart();    // turn on paging
  procinit();       // process table
  trapinithart();   // install kernel trap vector
  plicinithart();   // ask PLIC for device interrupts

  printf("Here\n");
  scheduler();
}