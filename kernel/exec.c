#include "types.h"
#include "riscv.h"
#include "exec.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "topology.h"
#include "sbi.h"


// TODEL
extern ptr_t ksize;
extern ptr_t p_entry;
extern void _masters_boot(void);
extern unsigned long uart0;

extern void _slaves_boot(void);
extern void _masters_wakeup(void);
extern pagetable_t kernel_pagetable;
extern ptr_t dtb_pa;

// Array of kernel images paths
char* kimg[NB_SOCKETS] = {[0 ... NB_SOCKETS-1] = "/kernel"};
unsigned int kimg_id;

static int loadseg(pde_t*, uint64, struct inode*, uint, uint, char);

struct map121_args{
  pagetable_t pgt;
  struct memrange* mr;
};

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
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz, 1) < 0)
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


void
map121(void* memrange, void* param)
{
  struct memrange* mr = memrange;
  struct map121_args* args = param;

  ptr_t start = (ptr_t)mr->start;
  ptr_t size = mr->length;
  int perm = PTE_R;
  pte_t* pte;

  // // Permissions
  if(mr->domain == args->mr->domain)
    perm |= PTE_W;
  if(mr->reserved)
    perm |= PTE_X;

  while(size >= PGSIZE){
    // Avoid the aready mapped kernel
    pte = walk(args->pgt, start, 0);
    if(!pte || !*pte){
      kvmmap(args->pgt, start, start, PGSIZE, perm);
    }
    start += PGSIZE;
    size -= PGSIZE;
  }
  if(size)
    kvmmap(args->pgt, start, start, size, perm);
}


uint64
map2(pagetable_t pagetable, uint64 from, uint64 to, uint64 paddr)
{
  if(paddr != PGROUNDUP(paddr))
    panic("map2: unaligned physical address");

  from = PGROUNDDOWN(from);
  pte_t* pte;

  for(uint64 cur=from; cur < to; cur += PGSIZE){
    if(!((pte = walk(pagetable, cur, 0)) && PTE2PA(*pte))){
      memset((void*)paddr, 0, PGSIZE);
      kvmmap(pagetable, cur, paddr, PGSIZE, PTE_R|PTE_W|PTE_X);
      paddr += PGSIZE;
    }
  }
  
  return paddr;
}

pagetable_t
kload(void* domain, char* kimg, struct memrange* mr)
{
  struct domain* d = domain;

  int i, off;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0;

  begin_op();

  if((ip = namei(kimg)) == 0){
    end_op();
    return 0;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;
  
  
  // Get an arbitrary memory range large enough to place the kernel
  // TODO: Compute ksize from elf here
  for(; mr && mr->length < ksize && mr->reserved; mr=mr->next);

  if(!mr){
    printf(
      "kexec: domain %d has not enought memory for the kernel (need %dB)\n",
      d->domain_id, ksize
    );
    panic("");
  }

  char* vaddr = mr->start;

  if((pagetable = uvmcreate()) == 0)
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
    if((vaddr = (char*)map2(pagetable, ph.vaddr, ph.vaddr + ph.memsz, (ptr_t)vaddr)) == 0)
      goto bad;
    if((ph.vaddr % PGSIZE) != 0)
      goto bad;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz, 0) < 0)
      goto bad;
    vaddr += ph.memsz;
  }

  
  iunlockput(ip);
  end_op();

  struct map121_args args;
  args.pgt = pagetable;
  args.mr = mr;
  forall_memrange(map121, &args);

  return pagetable;

  bad:
  if(pagetable)
    proc_freepagetable(pagetable, vaddr-(char*)mr->start);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return 0;
}


// load the kernel from the disk in the memory of another hart and start
// this hart on the new kernel text
void kexec(void* memrange, void* args){
  struct memrange* mr = memrange;
  pagetable_t pgt = args;

  // Information is stored right after the kernel
  struct boot_arg* bargs = (struct boot_arg*)((char*)mr->start+mr->length) - 1;
  bargs->dtb_pa = dtb_pa;
  bargs->current_domain = mr->domain->domain_id;

  // TODO: read the _entry symbol from ELF
  bargs->entry = (ptr_t)_masters_boot + (ptr_t)mr->start - p_entry;

  // Make new kernel first page executable in both pagetables
  vmperm(kernel_pagetable, PGROUNDDOWN(bargs->entry), PGSIZE, PTE_X, 1);
  vmperm(pgt, PGROUNDDOWN(bargs->entry), PGSIZE, PTE_X, 1);

  // TODEL: this device is just mapped for debug printing  
  kvmmap(pgt, uart0, uart0, 0x100, PTE_R|PTE_W);

  // Wake up the domain master hart
  bargs->mksatppgt = MAKE_SATP(pgt);
}


// kexec a hart if it is not in the same domain as the caller
inline void
wakeup_masters(void* domain, void* args)
{
  struct domain* d = domain;
  if(my_domain() == d) return;
  ptr_t* sp = (ptr_t*)((char*)d->memranges->start + d->memranges->length);
  sbi_start_hart(d->cpus->lapic, (ptr_t)_masters_wakeup, (ptr_t)sp);
}


void
start_domain(void* domain, void* kimg)
{
  struct domain* d = domain;
  // Avoid caller's domain 
  if(d == my_domain()) return;
  pagetable_t pgt;
  struct memrange* mr = d->memranges;
  pgt = kload(domain, kimg, mr);

  kexec(mr, pgt);
}

void start_all_domains(void)
{
  // Load a kernel image in the memory of all domains
  forall_domain(start_domain, kimg[kimg_id++]);
  
  uint64_t i = 1<<29;
  for(; i; --i);
  printf("salut\n");
}


void
wakeup_slaves(void* c, void* args)
{
  struct cpu_desc* cpu = c;

  // Avoid domain master
  if(cpu->lapic == cpuid())
    return;

  // Allocate a stack and pass the pagetable through it
  ptr_t* stack = (ptr_t*)PGROUNDUP((ptr_t)kalloc()+1);
  *(stack-1) = (ptr_t)MAKE_SATP(args);

  sbi_start_hart(cpu->lapic, (ptr_t)_slaves_boot, (ptr_t)stack);
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz, char user)
{
  uint i, n;
  uint64 pa;
  pte_t* pte;

  for(i = 0; i < sz; i += PGSIZE){
    if(user)
      pa = walkaddr(pagetable, va + i);
    else{
      pte = walk(pagetable, va+i, 0);
      pa = pte? PTE2PA(*pte) : 0;
    }
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
