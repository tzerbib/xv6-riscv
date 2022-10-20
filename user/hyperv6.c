#include "kernel/types.h"
#include "user/user.h"

static inline uint64
r_vsstatus()
{
  uint64 x;
  asm volatile("csrr %0, vsstatus" : "=r" (x) );
  return x;
}

int main(int argc, char *argv[]) {
    printf("hello from userspace, pid %d\n", getpid());

    von();

    /*printf(">>>>>>>>> vsstatus = %x\n", r_vsstatus());*/

    exit(0);
}
