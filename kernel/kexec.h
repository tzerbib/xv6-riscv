#ifndef __KEXEC_H__
#define __KEXEC_H__

#include "types.h"


struct boot_arg{
  ptr_t p_entry;
  ptr_t dtb_pa;
  ptr_t current_domain;
};

void kexec(void*, void*);

#endif // __KEXEC_H__