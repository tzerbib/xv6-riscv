#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "topology.h"
#include "kexec.h"


extern ptr_t ksize;
extern void _entry(void);
extern void _boot(void);
extern char end[];                 // first address after kernel.
extern pagetable_t kernel_pagetable;
extern pagetable_t kexec_pagetable(struct domain*);


static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    sz = sz1;
    if((ph.vaddr % PGSIZE) != 0)
      goto bad;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate two pages at the next page boundary.
  // Use the second as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-2*PGSIZE);
  sp = sz;
  stackbase = sp - PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}


// Make a direct-map page table for the kernel.
extern char trampoline[]; // trampoline.S
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
  kvmmap(kpgtbl, (ptr_t)_entry, 0x82000000, ksize+PGSIZE, PTE_R|PTE_W|PTE_X);

  // End of memrange of domain 1
  kvmmap(kpgtbl, 0x82000000, 0x82000000, 0x2000000, PTE_R|PTE_W|PTE_X);

  // OpenSBI
  kvmmap(kpgtbl, 0x80000000, 0x80000000, (ptr_t)_entry-0x80000000, PTE_R|PTE_X);

  // End of domain 0
  kvmmap(kpgtbl, PGROUNDUP((ptr_t)_entry+ksize+PGSIZE), PGROUNDUP((ptr_t)_entry+ksize+PGSIZE), 0x82000000-((ptr_t)_entry+ksize+PGSIZE)-PGSIZE, PTE_R|PTE_X);


  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (ptr_t)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}


// load the kernel from the disk in the memory of another hart and start
// this hart on the new kernel text
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
  bargs->p_entry = (ptr_t)mr->start;
  printf("kexec: bargs = %p\n", bargs);

  // Wake up an arbitrary hart of the domain
  // ptr_t offset = _boot-_entry;
  // sbi_start_hart(d->cpus->lapic, (unsigned long)mr->start+offset, 0);
  sbi_start_hart(d->cpus->lapic, (ptr_t)_boot, MAKE_SATP(pgt));
}


// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
