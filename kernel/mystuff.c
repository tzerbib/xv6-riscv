#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"


// Number of ACPI cores
#define NBCPU       2

// Informations for ACPI Header
#define OEMID_VALUE "BOCHS "
#define OEM_TABLEID "BXPCSRAT"
#define CREATOR_ID  "BXPC"

// Flags for affinity structures
#define F_ENABLED (1 << 0)
#define F_HOTPLUG (1 << 1)
#define F_NOVOLAT (1 << 2)


// RSTD table Header
struct ACPISDTHeader {
  char Signature[4];
  uint32_t Length;
  uint8_t Revision;
  uint8_t Checksum;
  char OEMID[6];
  char OEMTableID[8];
  uint32_t OEMRevision;
  uint32_t CreatorID;
  uint32_t CreatorRevision;
};


// RSDT complete table 
struct RSDT {
  struct ACPISDTHeader h;
  uint32_t PointerToOtherSDT[];
};


// header of the SRAT table
struct SRAT
{
    char signature[4];   // Contains "SRAT"
    uint32_t length;     // Length of entire SRAT including entries
    uint8_t  rev;        // 3
    uint8_t  checksum;   // Entire table must sum to zero
    uint8_t  OEMID[6];   // What do you think it is?
    uint64_t OEMTableID; // For the SRAT it's the manufacturer model ID
    uint32_t OEMRev;     // OEM revision for OEM Table ID
    uint32_t creatorID;  // Vendor ID of the utility used to create the table
    uint32_t creatorRev; // Blah blah
 
    uint8_t reserved[12];
} __attribute__((packed));



// Static Resource Allocation Structures

// Processor Local APIC Affinity Structure
struct SRAT_proc_lapic_struct
{
    uint8_t type;      // 0x0 for this type of structure
    uint8_t length;    // 16
    uint8_t lo_DM;     // Bits [0:7] of the proximity domain
    uint8_t APIC_ID;   // Processor's APIC ID
    uint32_t flags;    // Haha the most useless thing ever
    uint8_t SAPIC_EID; // The processor's local SAPIC EID. Don't even bother.
    uint8_t hi_DM[3];  // Bits [8:31] of the proximity domain
    uint32_t _CDM;     // The clock domain which the processor belongs to (more jargon)
} __attribute__((packed));



// Processor Local x2APIC Affinity Structure
struct SRAT_proc_lapic2_struct
{
    uint8_t type;         // 0x2 for this type of structure
    uint8_t length;       // 24
    uint8_t reserved1[2]; // Must be zero
    uint32_t domain;      // The proximity domain which the logical processor belongs to
    uint32_t x2APIC_ID;   // Processor's x2APIC ID
    uint32_t flags;       // Haha the most useless thing ever
    uint32_t _CDM;        // The clock domain which the processor belongs to (more jargon)
    uint8_t reserved2[4]; // Reserved.
} __attribute__((packed));



// Memory Affinity Structure
struct SRAT_mem_struct
{
    uint8_t type;         // 0x1 for this type of structure
    uint8_t length;       // 40
    uint32_t domain;      // The domain to which this memory region belongs to
    uint8_t reserved1[2]; // Reserved
    uint32_t lo_base;     // Low 32 bits of the base address of the memory range
    uint32_t hi_base;     // High 32 bits of the base address of the memory range
    uint32_t lo_length;   // Low 32 bits of the length of the range
    uint32_t hi_length;   // High 32 bits of the length
    uint8_t reserved2[4]; // Reserved
    uint32_t flags;       // Flags
    uint8_t reserved3[8]; // Reserved
} __attribute__ ((packed));



// Kernels SRAT table, initialized in init_SRAT
struct SRAT* srat;


// Add an entry in the form of a local APIC in a given srat table
void add_local_APIC_affinity_struct(
  struct SRAT_proc_lapic_struct* s,
  uint8_t proximity_domain_low, uint8_t proximity_domain_high[3],
  uint8_t apic, uint32_t clock_domain
){
  s->type = (uint8_t) 0;
  s->length = 16;
  s->lo_DM = proximity_domain_low;
  s->APIC_ID = apic;
  s->flags = (uint32_t) F_ENABLED;
  strncpy((char*)&s->hi_DM, (char*)proximity_domain_high, 6);
  s->_CDM = clock_domain;
}


void add_memory_affinity_structure(
  struct SRAT_mem_struct* s,
  uint32_t domain,
  uint32_t addr_low, uint32_t addr_high,
  uint32_t length_low, uint32_t length_high,
  uint32_t flags
){
  s->type = (uint8_t) 1;
  s->length = 40;
  s->domain = domain;
  s->lo_base = addr_low;
  s->hi_base = addr_high;
  s->lo_length = length_low;
  s->hi_length = length_high;
  s->flags = flags;
}


uint8_t compute_checksum(void* p, uint32_t size){
  uint8_t sum = 0;

  for(uint32_t i=0; i<size; ++i){
    sum += ((char*) p)[i];
  }

  return (~sum)+1;
}


void* init_SRAT_manual(){
  // Allocate a page for SRAT
  srat = (struct SRAT*) kalloc();
  memset(srat, 0, PGSIZE);

  // Initialize SRAT header
  strncpy(srat->signature, "SRAT", 4);
  srat->rev = (uint8_t) 3;
  strncpy((char*)&srat->OEMID, OEMID_VALUE, 6);
  strncpy((char*)&srat->OEMTableID, OEM_TABLEID, 16);
  srat->OEMRev = (uint32_t) 2;
  strncpy((char*)&srat->creatorID, CREATOR_ID, 8);
  srat->creatorRev = (uint32_t) 1;

  
  // Populate
  uint8_t domain_high[3] = {0,0,0};

  for(uint id=0; id<NBCPU; ++id){
    // Add CPU
    add_local_APIC_affinity_struct(
      (struct SRAT_proc_lapic_struct*)((char*)(srat + sizeof(struct SRAT) + id*sizeof(struct SRAT_proc_lapic_struct))),
      id, domain_high,
      id, 0
    );

    // Add associated memory
    add_memory_affinity_structure(
      (struct SRAT_mem_struct*) ((char*)(srat + sizeof(struct SRAT) + NBCPU*sizeof(struct SRAT_proc_lapic_struct) + id*sizeof(struct SRAT_mem_struct))),
      0,
      0 + id*(0x100000), 0,
      0x100000, 0,
      F_ENABLED | F_NOVOLAT
    );
  }


  // Compute SRAT length and checksum
  srat->length = sizeof(struct SRAT) + NBCPU*(sizeof(struct SRAT_proc_lapic_struct) + sizeof(struct SRAT_mem_struct));
  srat->checksum = compute_checksum(srat, srat->length);

  return srat;
}


void* init_SRAT_x86(){
  // Allocate a page for SRAT
  srat = (struct SRAT*) kalloc();
  memset(srat, 0, PGSIZE);

  uint8_t srat2[] = {
    83, 82, 65, 84, 0, 1, 0, 0, 1, 63, 66, 79, 67, 72, 83, 32,
    66, 88, 80, 67, 32, 32, 32, 32, 1, 0, 0, 0, 66, 88, 80, 67,
    1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 16, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 16, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 16, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 0, 0, 0, 0, 0,
    0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 240, 15, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 40, 1, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0,
    0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };


  for(int i=0; i<sizeof(srat2); ++i){
    ((uint8_t*)srat)[i] = srat2[i];
  }

  return srat;
}


void* init_SRAT(){
  return init_SRAT_x86();
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
        printf("\tProximity domain (high): %d %d %d\n", ((struct SRAT_proc_lapic_struct*)curr)->hi_DM[0], ((struct SRAT_proc_lapic_struct*)curr)->hi_DM[1], ((struct SRAT_proc_lapic_struct*)curr)->hi_DM[2]);
        printf("\tAPIC ID: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->APIC_ID);
        printf("\tFlags: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->flags);
        printf("\tSAPIC EID: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->SAPIC_EID);
        printf("\tClock domain: %d\n", ((struct SRAT_proc_lapic_struct*)curr)->_CDM);
        printf("\n");
        break;
      }
      case 0x1:{
        printf("Memory Affinity Structure:\n");
        printf("Type: %d\n", ((struct SRAT_mem_struct*)curr)->type);
        printf("Length: %d\n", ((struct SRAT_mem_struct*)curr)->length);
        printf("Proximity domain: %d\n", ((struct SRAT_mem_struct*)curr)->domain);
        printf("Memory range base addr (low): %d\n", ((struct SRAT_mem_struct*)curr)->lo_base);
        printf("Memory range base addr (high): %d\n", ((struct SRAT_mem_struct*)curr)->hi_base);
        printf("Length of the range (low): %d\n", ((struct SRAT_mem_struct*)curr)->lo_length);
        printf("Length of the range (high): %d\n", ((struct SRAT_mem_struct*)curr)->hi_length);
        printf("Flags: %d\n", ((struct SRAT_mem_struct*)curr)->flags);
        printf("\n");
        break;
      }
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
