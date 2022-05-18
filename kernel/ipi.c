#include "types.h"
#include "riscv.h"
#include "defs.h"


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
#define SBI_EXT_HSM_HART_STOP            1

#define SBI_SEND_IPI                     0x4

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

  register uint64_t a0 asm ("a0") = (uint64_t)(arg0);
  register uint64_t a1 asm ("a1") = (uint64_t)(arg1);
  register uint64_t a2 asm ("a2") = (uint64_t)(arg2);
  register uint64_t a3 asm ("a3") = (uint64_t)(arg3);
  register uint64_t a4 asm ("a4") = (uint64_t)(arg4);
  register uint64_t a5 asm ("a5") = (uint64_t)(arg5);
  register uint64_t a6 asm ("a6") = (uint64_t)(fid);
  register uint64_t a7 asm ("a7") = (uint64_t)(ext);
  asm volatile ("ecall"
              : "+r" (a0), "+r" (a1)
              : "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6), "r" (a7)
              : "memory");
  ret.error = a0;
  ret.value = a1;

  return ret;
}


// Print the SBI specification version
void sbi_get_spec_version(){
  struct sbiret ret;
  ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);

  // Error case
  if(ret.error != 0){
    panic("sbi_get_spec_version");
  }

  uint32_t major = (ret.value >> 24) & 0x7f;
  uint32_t minor = ret.value & ~(0x7 << 24);

  printf("Sbi specification version:\n\tMajor: %d\n\tMinor: %d\n", major, minor);
}



void sbi_start_hart(const unsigned long hart_id, unsigned long addr, unsigned long a1){
  struct sbiret ret;
  ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_START, hart_id, addr, a1, 0, 0, 0);

  // Error case
  switch(ret.error){
    case SBI_SUCCESS: {
        printf("Start signal sended to hart %d\n", hart_id);
        return;
    }
    case SBI_ERR_INVALID_ADDRESS: {
      printf("Problem in the given starting address for hart %d\n", hart_id);
      break;
    }
    case SBI_ERR_INVALID_PARAM: {
      printf("Hart %d cannot be started in S mode\n", hart_id);
      break;
    }
    case SBI_ERR_ALREADY_AVAILABLE: {
      printf("Hart %d is already started\n", hart_id);
      break;
    }
    case SBI_ERR_FAILED: {
      printf("Start request failed for unknown reasons\n");
      break;
    }
  }

  panic("sbi_start_hart");
}


void sbi_stop_hart(void){
  struct sbiret ret;
  ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_STOP, 0, 0, 0, 0, 0, 0);

  if(ret.error == SBI_ERR_FAILED){
    panic("Failed to stop currently running hart");
  }
}


void sbi_send_ipi(const unsigned long *hart_mask){
  struct sbiret ret;
  ret = sbi_ecall(SBI_SEND_IPI, 0, (unsigned long)hart_mask, 0, 0, 0, 0, 0);

  // Error case
  if(ret.error != SBI_SUCCESS){
    panic("sbi_send_ipi");
  }

  printf("IPI sent!\n");
}