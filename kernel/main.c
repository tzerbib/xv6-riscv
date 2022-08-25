#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "topology.h"
#include "exec.h"
#include "sbi.h"
#include "memlayout.h"


char* p_entry;                     // first physical address of kernel.
ptr_t ksize;
extern void _entry(void);
extern char end[];                 // first address after kernel.
extern struct machine* machine;    // Beginning of whole machine structure
extern pagetable_t kernel_pagetable;
extern char numa_ready;
ptr_t dtb_pa;


// _entry jumps here in supervisor mode on CPU 0.
void
machine_master_main(unsigned long hartid, ptr_t dtb)
{
  ksize = (ptr_t)end - (ptr_t)_entry;
  dtb_pa = dtb;

  initialize_fdt((void*)dtb_pa);

  kinit();           // physical page allocator
  kvminit();         // create kernel page table
  init_topology(0);
  add_numa();
  numa_ready = 1;  // switch to kalloc_numa
  assign_freepages((void*) dtb_pa);

  dtb_kvmmake(kernel_pagetable); // Map uart registers, virtio mmio disk interface and plic
  consoleinit();
  printfinit();
  printf("\n");
  printf("xv6 kernel is booting\n");

  printf("hart %d as machine master\n", cpuid());

  print_dtb();

  print_topology();
  print_struct_machine_loc();

  // Ensure that the kernel text is in the local memory
  struct memrange* mr = find_memrange(machine, machine_master_main);
  if(mr->domain != my_domain())
    panic("kernel text is on a distant memory range: unimplemented");

  // Add execute permission to the kernel text
  if(vmperm(kernel_pagetable, (pte_t)_entry, ksize, PTE_X, 1)){
    printf("kernel text for domain %d is not mapped\n", mr->domain);
    panic("");
  }

  kvminithart();   // turn on paging
  timerinit();     // ask for clock interrupts
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

  // Wake up all other domain masters by sending IPIs
  forall_domain(wakeup_masters, 0);

  scheduler();
}


// _entry jumps here in supervisor mode on all domain master CPUs.
void domain_master_wakeup(unsigned long hartid)
{
  kvminithart();    // turn on paging
  trapinithart();   // install kernel trap vector
  plicinithart();   // ask PLIC for device interrupts

  struct memrange* mr = ((struct domain*)my_domain())->memranges;
  struct boot_arg* args = (struct boot_arg*)((char*)mr->start + mr->length) - 1;
  args->entry = args->mksatppgt = 0;

  while(!args->entry || !args->mksatppgt);
  ((void(*)(unsigned long, ptr_t, ptr_t))args->entry)(hartid, args->mksatppgt, (ptr_t)args);
}


void
domain_master_main(ptr_t args)
{
  struct boot_arg* tmp_bargs = (void*)args;
  struct boot_arg bargs;
  bargs.dtb_pa = tmp_bargs->dtb_pa;
  bargs.current_domain = tmp_bargs->current_domain;

  ksize = (ptr_t)end - (ptr_t)_entry;
  dtb_pa = bargs.dtb_pa;

  initialize_fdt((void*)dtb_pa);

  kinit();           // physical page allocator
  kvminit();         // create kernel page table
  init_topology(bargs.current_domain);
  add_numa();
  numa_ready = 1;  // switch to kalloc_numa
  assign_freepages((void*)bargs.dtb_pa);
  dtb_kvmmake(kernel_pagetable); // Map uart registers, virtio mmio disk interface and plic
  consoleinit();
  printfinit();

  printf("hart %d as domain master\n", cpuid());

  print_topology();
  print_struct_machine_loc();

  // Add execute permission to the kernel text
  if(vmperm(kernel_pagetable, (pte_t)_entry, ksize, PTE_X, 1)){
    printf("kernel text for domain %d is not mapped\n", bargs.current_domain);
    panic("");
  }

  // Map the new kernel at _entry (0x80200000)
  uvmunmap(kernel_pagetable, (ptr_t)_entry, 1+ksize/PGSIZE, 0);
  kvmmap(kernel_pagetable, (ptr_t)_entry, (ptr_t)p_entry, ksize, PTE_R|PTE_W|PTE_X);

  kvminithart();    // turn on paging
  timerinit();     // ask for clock interrupts
  trapinit();      // trap vectors
  trapinithart();   // install kernel trap vector
  plicinit();      // set up interrupt controller
  plicinithart();   // ask PLIC for device interrupts
  binit();         // buffer cache
  iinit();         // inode table
  fileinit();      // file table
  virtio_disk_init(); // emulated hard disk

  // Wake up all slaves by sending IPIs
  forall_cpu_in_domain(my_domain(), wakeup_slaves, (void*)kernel_pagetable);
  
  scheduler();
}


// _entry jumps here in supervisor mode on all domain master CPUs.
void
slave_main(void)
{
  printf("hart %d as slave\n", cpuid());

  trapinithart();   // install kernel trap vector
  plicinithart();   // ask PLIC for device interrupts

  printf("GOOD!\n");
  scheduler();
}