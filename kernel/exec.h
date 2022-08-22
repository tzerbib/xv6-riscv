#ifndef __KEXEC_H__
#define __KEXEC_H__

#include "topology.h"
#include "types.h"
#include "riscv.h"

struct boot_arg{
  ptr_t dtb_pa;
  ptr_t current_domain;
  ptr_t mksatppgt;
  ptr_t entry;
};

pagetable_t kload(void*, char*, struct memrange*);
void kexec(void*, void*);
void start_domain(void*, void*);
void start_all_domains(void);
void wakeup_masters(void*, void*);
void wakeup_slaves(void*, void*);


#endif // __KEXEC_H__