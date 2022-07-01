#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "sbi.h"

// Print the SBI specification version
struct sbi_spec_version sbi_get_spec_version(){
  struct sbiret ecall_ret;
  struct sbi_spec_version ret;

  ecall_ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);

  // Error case
  if(ecall_ret.error != 0)
    panic("sbi_get_spec_version");

  ret.major = (ecall_ret.value >> 24) & 0x7f;
  ret.minor = ecall_ret.value & ~(0x7 << 24);

  return ret;
}

void sbi_start_hart(uint32 hart_id, uint64 addr, uint64 a1){
  struct sbiret ecall_ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_START, hart_id, addr, a1, 0, 0, 0);

  switch(ecall_ret.error) {
    case SBI_SUCCESS: {
      printf("Start signal sended to hart %d\n", hart_id);
      return;
    }
    case SBI_ERR_INVALID_ADDRESS: {
      printf("Problem in the given starting address for hart %d\n", hart_id);
      panic("sbi_start_hart");
    }
    case SBI_ERR_INVALID_PARAM: {
      printf("Hart %d cannot be started in S mode\n", hart_id);
      panic("sbi_start_hart");
    }
    case SBI_ERR_ALREADY_AVAILABLE: {
      printf("Hart %d is already started\n", hart_id);
      panic("sbi_start_hart");
    }
    case SBI_ERR_FAILED: {
      printf("Start request failed for unknown reasons\n");
      panic("sbi_start_hart");
    }
    default:
      panic("sbi_start_hart");
  }
}

void sbi_send_ipi(const unsigned long *hart_mask){
  struct sbiret ecall_ret = sbi_ecall(SBI_SEND_IPI, 0, (unsigned long)hart_mask, 0, 0, 0, 0, 0);

  if(ecall_ret.error != SBI_SUCCESS)
    panic("sbi_send_ipi");
}

void sbi_set_timer(uint64 stime_value) {
  struct sbiret ecall_ret = sbi_ecall(SBI_EXT_TIMER, SBI_TIMER_SET_TIMER, stime_value, 0, 0, 0, 0, 0);

  if (ecall_ret.error != SBI_SUCCESS)
      panic("sbi_set_timer");
}
