#include "types.h"
#include <stdint.h>


struct machine{
  struct cpu_desc* all_cpus;   // Linked list of all cpu in the machine
  struct memrange* all_ranges; // Linked list of memory ranges of the machine
  struct domain* all_domains;  // Linked list of all numa domains of the machine
}__attribute__((packed));


struct cpu_desc{
  struct cpu_desc* all_next;   // Next cpu in the list of all cpus
  struct cpu_desc* next;       // Next cpu on the same domain
  struct domain* domain;       // Domain of the cpu
  uint32_t lapic;              // APIC ID
}__attribute__((packed));


struct memrange{
  void* start;                 // Virtual starting address of the memory range
  uint64_t length;             // Length of the memory range
  struct domain* domain;       // Domain associated with this memory range
  struct memrange* next;       // Next memory range on the same domain
  struct memrange* all_next;   // Next memory range in the global list
}__attribute__((packed));


struct domain{
  uint32_t domain_id;          // Proximity domain (low and high bits)
  struct domain* all_next;     // Next numa domain in the list of all domains
  struct memrange* memranges;  // First memory range of this numa domain
  struct cpu_desc* cpus;       // First cpu of this numa domain 
  struct page_t* freepages;    // First free page for this domain
}__attribute__((packed));
