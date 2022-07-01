#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "ecall.h"

// Print the SBI specification version
void sbi_get_spec_version(){
  struct sbiret ret;
  ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);

  // Error case
  if(ret.error != 0){
    panic("sbi_get_spec_version");
  }

  uint32 major = (ret.value >> 24) & 0x7f;
  uint32 minor = ret.value & ~(0x7 << 24);

  printf("SBI specification version:\n\tMajor: %d\n\tMinor: %d\n", major, minor);
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

void sbi_send_ipi(const unsigned long *hart_mask){
  struct sbiret ret;
  ret = sbi_ecall(SBI_SEND_IPI, 0, (unsigned long)hart_mask, 0, 0, 0, 0, 0);

  // Error case
  if(ret.error != SBI_SUCCESS){
    panic("sbi_send_ipi");
  }

  printf("IPI sent!\n");
}
