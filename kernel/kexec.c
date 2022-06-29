#include "kexec.h"
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "topology.h"


extern ptr_t ksize;
extern void _entry(void);
extern void _boot(void);
extern char end[];                 // first address after kernel.
extern pagetable_t kernel_pagetable;
extern char trampoline[]; // trampoline.S
extern char stack0[];


// Make a direct-map page table for the kernel.
pagetable_t
kexec_pagetable(struct domain* d)
{
  pagetable_t kpgtbl;
  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  //[numa] There is one PLIC per NUMA socket, so map them all.
  kvmmap(kpgtbl, PLIC, PLIC, NB_SOCKETS*PLIC_SZ, PTE_R | PTE_W);


  // kernel text tmp mapping from va in hart 0 to physical in hart 3
  kvmmap(kpgtbl, (ptr_t)_entry, 0x82000000, ksize, PTE_R|PTE_W|PTE_X);

  // End of memrange of domain 1
  kvmmap(kpgtbl, 0x82000000, 0x82000000, 0x2000000, PTE_R|PTE_W|PTE_X);

  // OpenSBI
  kvmmap(kpgtbl, 0x80000000, 0x80000000, (ptr_t)_entry-0x80000000, PTE_R|PTE_X);

  // End of domain 0
  kvmmap(kpgtbl, PGROUNDUP((ptr_t)_entry+ksize), PGROUNDUP((ptr_t)_entry+ksize), 0x82000000-((ptr_t)_entry+ksize)-PGSIZE+1, PTE_R|PTE_X);


  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (ptr_t)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}


void kexec(void* domain, void* args){
  struct domain* d = domain;
  struct memrange *mr;


  // Avoid domain from main boot hart
  if(my_domain() == d) return;

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

  // Add execute permission to the kernel
  if(vmperm((pte_t)mr->start, ksize, PTE_X, 1)){
    printf("kernel text for domain %d is not mapped\n", mr->domain);
    panic("");
  }

  pagetable_t pgt = kexec_pagetable(d);

  // Information is stored right after the kernel
  struct boot_arg* bargs = (void*)PGROUNDUP((ptr_t)mr->start+ksize);
  bargs->dtb_pa = (ptr_t)args;
  bargs->current_domain = mr->domain->domain_id;
  // bargs->pgt = MAKE_SATP(pgt);
  printf("kexec: bargs = %p\n", bargs);

  // Wake up an arbitrary hart of the domain
  // ptr_t offset = _boot-_entry;
  // sbi_start_hart(d->cpus->lapic, (unsigned long)mr->start+offset, 0);
  sbi_start_hart(d->cpus->lapic, (ptr_t)_boot, MAKE_SATP(pgt));
}

