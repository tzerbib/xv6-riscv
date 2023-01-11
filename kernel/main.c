#include "communication.h"
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "topology.h"
#include "exec.h"
#include "sbi.h"
#include "memlayout.h"
#include "proc.h"


char* p_entry;                     // first physical address of kernel.
ptr_t ksize;
extern void _entry(void);
extern char end[];                 // first address after kernel.
extern struct machine* machine;    // Beginning of whole machine structure
extern pagetable_t kernel_pagetable;
extern char numa_ready;
ptr_t dtb_pa;
extern struct proc* allocproc(void);
extern struct proc *initproc;

void
kernel_proc()
{
  struct proc* p = myproc();
  release(&p->lock);

  initcomm();         // communication buffer
  binit();            // buffer cache
  iinit();            // inode table
  fileinit();         // file table
  virtio_disk_init(); // emulated hard disk

  if(IS_MMASTER){
    __sync_synchronize();

    // Wake up all other domain masters by sending IPIs (on this kernel image)
    // Mandatory to be sure that IRQ from disk will be held by some hart
    forall_domain(machine, wakeup_masters, 0);

    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep)
    fsinit(ROOTDEV);

    // Load kernel images into memory and start all domain masters on them
    start_all_domains();

    // Machine master
    userinit();       // first user process
  }
  else{
    // Domain masters
    consoleinit();
    printfinit();

    printf("hart %d as domain master\n", cpuid());

    print_topology();
    print_struct_machine_loc();

    __sync_synchronize();

    // Wake up all slaves by sending IPIs
    forall_cpu_in_domain(my_domain(), wakeup_slaves, (void*)kernel_pagetable);
  }

  // Wait for the creation of initproc by one of the kernel

  // Release the process
  acquire(&p->lock);
  p->xstate = 0;
  p->state = ZOMBIE;
  p->parent = initproc;

  // Exit kernel proc
  for(;;);
  sched();
  panic("kernel proc exit");
}

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
  add_numa(0);
  numa_ready = 1;    // switch to kalloc_numa
  assign_freepages((void*) dtb_pa);

  dtb_kvmmake(kernel_pagetable, 0); // Map uart registers, virtio mmio disk interface and plic
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

  // Create a main proc in the proctable (enables context switches)
  struct proc *p;
  p = allocproc();
  p->context.ra = (ptr_t)kernel_proc;
  safestrcpy(p->name, "kernel_proc", sizeof(p->name));
  p->state = RUNNABLE;
  release(&p->lock);

  scheduler();
}


// _entry jumps here in supervisor mode on all domain master CPUs.
// In this function, domain master do the bare minimum to be able
// to catch interruptions (so that the machine master can load kernel images
// from disk).
// Once all kernel images have been load by kexec (in domain 0),
// jump to _masters_boot (entry.S)
void domain_master_wakeup(unsigned long hartid)
{
  kvminithart();    // turn on paging
  trapinithart();   // install kernel trap vector

  struct memrange* mr = find_memrange((struct machine*)machine, ((struct domain*)my_domain())->combuf);
  struct boot_arg* tmp_args = (struct boot_arg*)PGROUNDDOWN((ptr_t)((char*)mr->start+mr->length-1));
  tmp_args->ready = 0;
  
  plicinithart();   // ask PLIC for device interrupts

  // Synchronization barrier
  while(!tmp_args->ready);
  __sync_synchronize();

  struct boot_arg* args = (struct boot_arg*)((char*)mr->start + mr->length) - 1;

  args->dtb_pa = tmp_args->dtb_pa;
  args->current_domain = tmp_args->current_domain;
  args->entry = tmp_args->entry;
  args->mksatppgt = tmp_args->mksatppgt;
  args->pgt = tmp_args->pgt;
  args->topology = tmp_args->topology;

  ((void(*)(unsigned long, ptr_t, ptr_t))args->entry)(hartid, args->mksatppgt, (ptr_t)args);
}



void
domain_master_main(ptr_t args)
{
  struct boot_arg* tmp_bargs = (void*)args;
  struct boot_arg bargs;
  bargs.dtb_pa = tmp_bargs->dtb_pa;
  bargs.current_domain = tmp_bargs->current_domain;
  bargs.pgt = tmp_bargs->pgt;
  struct machine* m = machine = (struct machine*)tmp_bargs->topology;

  ksize = (ptr_t)end - (ptr_t)_entry;
  dtb_pa = bargs.dtb_pa;

  initialize_fdt((void*)dtb_pa);

  kinit();           // physical page allocator
  kvminit();         // create kernel page table
  init_topology(bargs.current_domain);
  add_numa(m);
  numa_ready = 1;  // switch to kalloc_numa
  assign_freepages((void*)bargs.dtb_pa);
  dtb_kvmmake(kernel_pagetable, (void*)bargs.pgt); // Map uart registers, virtio mmio disk interface and plic
  
  // Add execute permission to the kernel text
  if(vmperm(kernel_pagetable, (pte_t)_entry, ksize, PTE_X, 1)){
    printf("kernel text for domain %d is not mapped\n", bargs.current_domain);
    panic("");
  }

  // Map the new kernel at _entry (0x80200000)
  uvmunmap(kernel_pagetable, (ptr_t)_entry, 1+ksize/PGSIZE, 0);
  kvmmap(kernel_pagetable, (ptr_t)_entry, (ptr_t)p_entry, ksize, PTE_R|PTE_W|PTE_X);

  kvminithart();      // turn on paging
  timerinit();        // ask for clock interrupts
  procinit();         // process table
  trapinit();         // trap vectors
  trapinithart();     // install kernel trap vector
  plicinit();         // set up interrupt controller
  plicinithart();     // ask PLIC for device interrupts

  // Create a main proc in the proctable (enables context switches)
  struct proc *p;
  p = allocproc();
  p->context.ra = (ptr_t)kernel_proc;
  safestrcpy(p->name, "kernel_proc", sizeof(p->name));
  p->state = RUNNABLE;
  release(&p->lock);

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