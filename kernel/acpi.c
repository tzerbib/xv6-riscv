#include "acpi.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"


// Kernels SRAT table, initialized in init_SRAT
struct SRAT* srat;


void* init_SRAT(){
  // Allocate a page for SRAT
  srat = (struct SRAT*) kalloc();
  memset(srat, 0, PGSIZE);

  uint8_t srat2[] = {
    // Original SRAT table imported from qemu-system-x86
    // 83, 82, 65, 84, 0, 1, 0, 0, 1, 63, 66, 79, 67, 72, 83, 32,
    // 66, 88, 80, 67, 32, 32, 32, 32, 1, 0, 0, 0, 66, 88, 80, 67,
    // 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0, 16, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0, 16, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0, 16, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 1, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    // 0, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 0, 0, 0, 0, 0,
    // 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 240, 15, 0, 0, 0, 0,
    // 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 1, 40, 1, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0,
    // 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    // 0, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 0, 0, 0, 0, 0,
    // 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

    // Modified SRAT table
    83, 82, 65, 84, 0, 1, 0, 0, 1, 63, 66, 79, 67, 72, 83, 32,  // begin --> OEMID
    66, 88, 80, 67, 32, 32, 32, 32, 1, 0, 0, 0, 66, 88, 80, 67, // OEMTableID -> CreatorID
    1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             // CreatorRevision -> reserved
    0, 16, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,            // Proc
    0, 16, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,            // Proc
    0, 16, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,            // Proc
    1, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0,            // Mem: type -> hi_base
    0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,            //      lo_length -> flags
    0, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 0, 0, 0, 0, 0,            //      reserved + Mem
    0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 40, 1, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0,
    0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 0, 0, 0, 0, 0,

    // De base
    0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0,
    
    // Marche un peu puis crash (adresses après UART et VIRT0)
    // 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    // Retourne 0x0 a chaque fois
    // 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    // Retourne 0xffffffffffffffff a chaque fois
    // 0, 0, 0, 48, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,

    
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };


  for(int i=0; i<sizeof(srat2); ++i){
    ((uint8_t*)srat)[i] = srat2[i];
  }

  return srat;
}


void print_srat(void* ptr){
  struct SRAT* srat = (struct SRAT*) ptr;

  char sig[5] = {
    srat->signature[0],
    srat->signature[1],
    srat->signature[2],
    srat->signature[3],
    0
  };

  char oemid[7] = {
    srat->OEMID[0],
    srat->OEMID[1],
    srat->OEMID[2],
    srat->OEMID[3],
    srat->OEMID[4],
    srat->OEMID[5],
    0
  };

  printf("Signature: %s\n", sig);
  printf("Length: %d\n", srat->length);
  printf("Revision: %d\n", srat->rev);
  printf("Checksum: %d\n", srat->checksum);
  printf("OEMID: %s\n", oemid);
  printf("OEM Table ID: %d\n", srat->OEMTableID);
  printf("OEM Revision: %d\n", srat->OEMRev);
  printf("Creator ID: %d\n", srat->creatorID);
  printf("Creator ID: %d\n", srat->creatorRev);
  printf("\n");

  uint8_t* curr = ((uint8_t*)srat) + sizeof(struct SRAT);
  
  // Print each subtable
  while(curr < ((uint8_t*)srat)+srat->length){
    switch(*curr){
      // Processor Local APIC Affinity Structure
      case 0x0:{
        printf("Processor Local APIC Affinity Structure:\n");
        printf("\tType: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->type);
        printf("\tLength: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->length);
        printf("\tProximity domain (low): %d\n", ((struct SRAT_proc_lapic_struct*)curr)->lo_DM);
        printf("\tProximity domain (high): %p%x%x\n", ((struct SRAT_proc_lapic_struct*)curr)->hi_DM[0], ((struct SRAT_proc_lapic_struct*)curr)->hi_DM[1], ((struct SRAT_proc_lapic_struct*)curr)->hi_DM[2]);
        printf("\tAPIC ID: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->APIC_ID);
        printf("\tFlags: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->flags);
        printf("\tSAPIC EID: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->SAPIC_EID);
        printf("\tClock domain: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->_CDM);
        printf("\n");
        break;
      }
      // Memory Affinity Structure
      case 0x1:{
        printf("Memory Affinity Structure:\n");
        printf("Type: %d\n", ((struct SRAT_mem_struct*)curr)->type);
        printf("Length: %d\n", ((struct SRAT_mem_struct*)curr)->length);
        printf("Proximity domain: %d\n", ((struct SRAT_mem_struct*)curr)->domain);
        printf("Memory range base addr (low): %p\n", ((struct SRAT_mem_struct*)curr)->lo_base);
        printf("Memory range base addr (high): %p\n", ((struct SRAT_mem_struct*)curr)->hi_base);
        printf("Length of the range (low): %p\n", ((struct SRAT_mem_struct*)curr)->lo_length);
        printf("Length of the range (high): %p\n", ((struct SRAT_mem_struct*)curr)->hi_length);
        printf("Flags: %d\n", ((struct SRAT_mem_struct*)curr)->flags);
        printf("\n");
        break;
      }
      // Processor Local x2APIC Affinity Structure
      case 0x2:{
        printf("Processor Local x2APIC Affinity Structure:\n");
        printf("Type: %d\n", ((struct SRAT_proc_lapic2_struct*)curr)->type);
        printf("Length: %d\n", ((struct SRAT_proc_lapic2_struct*)curr)->length);
        printf("Proximity domain: %d\n", ((struct SRAT_proc_lapic2_struct*)curr)->domain);
        printf("Processor x2APIC ID: %d\n", ((struct SRAT_proc_lapic2_struct*)curr)->x2APIC_ID);
        printf("Flags: %d\n", ((struct SRAT_proc_lapic2_struct*)curr)->flags);
        printf("Clock domain: %d\n", ((struct SRAT_proc_lapic2_struct*)curr)->_CDM);
        printf("\n");
        break;
      }
      default:{
        panic("Unknown SRAT subtable type!\n");
      }
    }
    
    // Add struct length
    curr += *(curr+1);
  }
}
