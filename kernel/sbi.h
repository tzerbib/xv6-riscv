#include "types.h"

#define SBI_SUCCESS                       (0)
#define SBI_ERR_FAILED                   (-1)
#define SBI_ERR_NOT_SUPPORTED            (-2)
#define SBI_ERR_INVALID_PARAM            (-3)
#define SBI_ERR_DENIED                   (-4)
#define SBI_ERR_INVALID_ADDRESS          (-5)
#define SBI_ERR_ALREADY_AVAILABLE        (-6)
#define SBI_ERR_ALREADY_STARTED          (-7)
#define SBI_ERR_ALREADY_STOPPED          (-8)

#define SBI_EXT_BASE                     0x10
#define SBI_EXT_BASE_GET_SPEC_VERSION    0x0
#define SBI_EXT_BASE_GET_IMPLEM_VERSION  0x1

#define SBI_EXT_HSM                      0x48534D
#define SBI_EXT_HSM_HART_START           0

#define SBI_SEND_IPI                     0x4

#define SBI_EXT_TIMER                    0x54494D45

#define SBI_TIMER_SET_TIMER              0x0


// Structure returned by every ecall
struct sbiret{
  long error;
  long value;
};

static inline struct sbiret sbi_ecall(
  int ext, int fid,
  unsigned long arg0, unsigned long arg1, unsigned long arg2,
  unsigned long arg3, unsigned long arg4, unsigned long arg5
){
  struct sbiret ret;

  register uint64 a0 asm ("a0") = (uint64)(arg0);
  register uint64 a1 asm ("a1") = (uint64)(arg1);
  register uint64 a2 asm ("a2") = (uint64)(arg2);
  register uint64 a3 asm ("a3") = (uint64)(arg3);
  register uint64 a4 asm ("a4") = (uint64)(arg4);
  register uint64 a5 asm ("a5") = (uint64)(arg5);
  register uint64 a6 asm ("a6") = (uint64)(fid);
  register uint64 a7 asm ("a7") = (uint64)(ext);
  asm volatile ("ecall"
              : "+r" (a0), "+r" (a1)
              : "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6), "r" (a7)
              : "memory");
  ret.error = a0;
  ret.value = a1;

  return ret;
}

struct sbi_spec_version {
  uint32 major;
  uint32 minor;
};

struct sbi_spec_version sbi_get_spec_version();
void sbi_start_hart(uint32 hart_id, uint64 addr, uint64 a1);
void sbi_send_ipi(const unsigned long *hart_mask);
void sbi_set_timer(uint64 stime_value);
