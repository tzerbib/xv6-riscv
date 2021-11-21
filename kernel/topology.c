#include "topology.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "acpi.h"
#include <stdint.h>


struct machine* machine;       // Beginning of whole machine structure
void* topology_end;            // Next empty place for machine substructures
int64_t remaining;            // Remaining size in the last topology page

void print_topology();


void init_topology(){
  machine = (struct machine*) kalloc();
  memset(machine, 0, PGSIZE);
  remaining = PGSIZE - sizeof(struct machine);

  // Point next empty space on newly allocated area
  topology_end = (uint8_t*)machine + sizeof(struct machine);
}


// Ensure that there is enought space for a structure to be added
void ensure_space(uint64_t length){
  if(remaining < length){
    topology_end = kalloc();
    memset(topology_end, 0, PGSIZE);
    remaining = PGSIZE;
  }
}


struct domain* add_domain(uint32_t id){
  // Ensure that all the structure will fit in one page
  ensure_space(sizeof(struct domain));
  struct domain* new_domain = (struct domain*) topology_end;

  // Fill the structure information
  new_domain->domain_id = id;

  // Link this new structure to the others
  new_domain->all_next = machine->all_domains;
  machine->all_domains = new_domain;

  // Compute next empty place
  topology_end += sizeof(struct domain);
  remaining -= sizeof(struct domain);

  return new_domain;
}


// Returns address of corresponding domain. Creates it if needed.
struct domain* find_domain(uint32_t id){
  struct domain* curr;
  
  // Browse all domains until finding the good one
  for(curr=machine->all_domains; curr; curr=curr->all_next){
    if(curr->domain_id == id){
      return curr;
    }
  }

  // Create new domain
  return add_domain(id);
}


void* add_cpu(uint32_t domain_id, uint32_t apic_id){
  // Ensure that all the structure will fit in one page
  ensure_space(sizeof(struct cpu_desc));
  struct cpu_desc* new_cpu = (struct cpu_desc*) topology_end;

  // Compute next empty place
  topology_end += sizeof(struct cpu_desc);
  remaining -= sizeof(struct cpu_desc);
  
  // Fill the structure information
  new_cpu->lapic = apic_id;
  struct domain* d = find_domain(domain_id);
  new_cpu->domain = d;
  new_cpu->next = d->cpus;
  d->cpus = new_cpu;

  // Link this new structure to the others
  new_cpu->all_next = machine->all_cpus;
  machine->all_cpus = new_cpu;

  return new_cpu;
}


void* add_memrange(uint32_t domain_id, void* start, uint64_t length){
  // Ensure that all the structure will fit in one page
  ensure_space(sizeof(struct memrange));
  struct memrange* new_memrange = (struct memrange*) topology_end;

  // Compute next empty place
  topology_end += sizeof(struct memrange);
  remaining -= sizeof(struct memrange);

  // Fill the structure information
  new_memrange->start = start;
  new_memrange->length = length;
  struct domain* d = find_domain(domain_id);
  new_memrange->domain = d;
  new_memrange->next = d->memranges;
  d->memranges = new_memrange;

  // Link this new structure to the others
  new_memrange->all_next = machine->all_ranges;
  machine->all_ranges = new_memrange;
  
  return new_memrange;
}


// Add a numa topology given by a SRAT table to the machine description
void add_numa(void* ptr){
  struct SRAT* srat = (struct SRAT*) ptr;
  uint8_t* curr = ((uint8_t*)srat) + sizeof(struct SRAT);

  while(curr < ((uint8_t*)srat)+srat->length){
    switch(*curr){
      // Processor Local APIC Affinity Structure
      case 0x0:{
        struct SRAT_proc_lapic_struct* lapic = (struct SRAT_proc_lapic_struct*)curr;
        if(!lapic->flags){break;}
        uint32_t domain_id = (lapic->hi_DM[2] << 24)
                           + (lapic->hi_DM[1] << 16)
                           + (lapic->hi_DM[0] << 8)
                           + lapic->lo_DM;
        add_cpu(domain_id, (uint32_t)lapic->APIC_ID);
        break;
      }
      // Memory Affinity Structure
      case 0x1:{
        struct SRAT_mem_struct* memrange = (struct SRAT_mem_struct*)curr;
        if(!memrange->flags){break;}
        uint32_t domain_id = memrange->domain;
        uint64_t addr = ((uint64_t)memrange->hi_base << 32) + memrange->lo_base;
        uint64_t length = ((uint64_t)memrange->hi_length << 32) + memrange->lo_length;
        add_memrange(domain_id, (void*)addr, length);
        break;
      }
      // Processor Local x2APIC Affinity Structure
      case 0x2:{
        struct SRAT_proc_lapic2_struct* lapic = (struct SRAT_proc_lapic2_struct*)curr;
        if(!lapic->flags){break;}
        add_cpu(lapic->domain, lapic->x2APIC_ID);
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


void print_cpu(struct cpu_desc* cpu){
  // printf("(%x) CPU id: %d\n", cpu, cpu->lapic);
  printf("(0x%x) CPU id %d\n", cpu, cpu->lapic);
}


void print_memrange(struct memrange* memrange){
  printf(
    "(0x%x) Memory range: 0x%x -- 0x%x\n",
    memrange, memrange->start, memrange->start + memrange->length
  );
}


// Print the topology of the entire machine
void print_topology(){
  struct domain* curr_dom;
  struct cpu_desc* curr_cpu;
  struct memrange* curr_memrange;

  if(!machine){
    return;
  }

  // Browse all domains
  for(curr_dom=machine->all_domains; curr_dom; curr_dom=curr_dom->all_next){
    printf("(0x%x) Numa domain 0x%x:\n", curr_dom, curr_dom->domain_id);

    // Browse all cpu of a given domain
    for(curr_cpu=curr_dom->cpus; curr_cpu; curr_cpu=curr_cpu->next){
      print_cpu(curr_cpu);
    }

    // Browse all memory ranges of a given domain
    for(curr_memrange=curr_dom->memranges; curr_memrange; curr_memrange=curr_memrange->next){
      print_memrange(curr_memrange);
    }

    printf("\n");
  }
}