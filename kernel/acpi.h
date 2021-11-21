#include "types.h"


// Number of ACPI cores
#define NBCPU       3

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
    char signature[4];    // Contains "SRAT"
    uint32_t length;      // Length of entire SRAT including entries
    uint8_t  rev;         // 3
    uint8_t  checksum;    // Entire table must sum to zero
    uint8_t  OEMID[6];    // What do you think it is?
    uint64_t OEMTableID;  // For the SRAT it's the manufacturer model ID
    uint32_t OEMRev;      // OEM revision for OEM Table ID
    uint32_t creatorID;   // Vendor ID of the utility used to create the table
    uint32_t creatorRev;  // Blah blah
 
    uint8_t reserved[12];
} __attribute__((packed));



// Static Resource Allocation Structures

// Processor Local APIC Affinity Structure
struct SRAT_proc_lapic_struct
{
    uint8_t type;          // 0x0 for this type of structure
    uint8_t length;        // 16
    uint8_t lo_DM;         // Bits [0:7] of the proximity domain
    uint8_t APIC_ID;       // Processor's APIC ID
    uint32_t flags;        // Haha the most useless thing ever
    uint8_t SAPIC_EID;     // The processor's local SAPIC EID. Don't even bother.
    uint8_t hi_DM[3];      // Bits [8:31] of the proximity domain
    uint32_t _CDM;         // The clock domain which the processor belongs to (more jargon)
} __attribute__((packed));



// Processor Local x2APIC Affinity Structure
struct SRAT_proc_lapic2_struct
{
    uint8_t type;          // 0x2 for this type of structure
    uint8_t length;        // 24
    uint8_t reserved1[2];  // Must be zero
    uint32_t domain;       // The proximity domain which the logical processor belongs to
    uint32_t x2APIC_ID;    // Processor's x2APIC ID
    uint32_t flags;        // Haha the most useless thing ever
    uint32_t _CDM;         // The clock domain which the processor belongs to (more jargon)
    uint8_t reserved2[4];  // Reserved.
} __attribute__((packed));



// Memory Affinity Structure
struct SRAT_mem_struct
{
    uint8_t type;          // 0x1 for this type of structure
    uint8_t length;        // 40
    uint32_t domain;       // The domain to which this memory region belongs to
    uint8_t reserved1[2];  // Reserved
    uint32_t lo_base;      // Low 32 bits of the base address of the memory range
    uint32_t hi_base;      // High 32 bits of the base address of the memory range
    uint32_t lo_length;    // Low 32 bits of the length of the range
    uint32_t hi_length;    // High 32 bits of the length
    uint8_t reserved2[4];  // Reserved
    uint32_t flags;        // Flags
    uint8_t reserved3[8];  // Reserved
} __attribute__ ((packed));
