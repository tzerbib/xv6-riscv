#include "topology.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "acpi.h"


extern struct kmem kmem;
extern char end[];             // first address after kernel.
extern pagetable_t kernel_pagetable;
extern char numa_ready;


struct machine* machine;       // Beginning of whole machine structure

struct {
  void* topology_end;          // Next empty place for machine substructures
  uint64_t remaining;          // Remaining size in the last topology page
}numa_allocator;


void init_topology(){
  machine = (struct machine*) kalloc();
  machine->all_cpus = 0;
  machine->all_ranges = 0;
  machine->all_domains = 0;
  numa_allocator.remaining = PGSIZE - sizeof(struct machine);

  // Point next empty space on newly allocated area
  numa_allocator.topology_end = (uint8_t*)machine + sizeof(struct machine);
}


// Ensure that there is enought space for a structure to be added
void ensure_space(uint64_t length){
  if(numa_allocator.remaining < length){
    numa_allocator.topology_end = kalloc();
    numa_allocator.remaining = PGSIZE;
  }
}


struct domain* add_domain(uint32_t id){
  // Ensure that all the structure will fit in one page
  ensure_space(sizeof(struct domain));
  struct domain* new_domain = (struct domain*) numa_allocator.topology_end;

  // Fill the structure information
  new_domain->domain_id = id;
  new_domain->memranges = 0;
  new_domain->cpus = 0;
  initlock(&new_domain->freepages.lock, "numa_freepage");
  new_domain->freepages.freelist = (void*)0;

  // Link this new structure to the others
  new_domain->all_next = machine->all_domains;
  machine->all_domains = new_domain;

  // Compute next empty place
  numa_allocator.topology_end += sizeof(struct domain);
  numa_allocator.remaining -= sizeof(struct domain);

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
  struct cpu_desc* new_cpu = (struct cpu_desc*) numa_allocator.topology_end;

  // Compute next empty place
  numa_allocator.topology_end += sizeof(struct cpu_desc);
  numa_allocator.remaining -= sizeof(struct cpu_desc);
  
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


void* find_memrange(void* addr){
  struct memrange* curr = machine->all_ranges;

  for(; curr; curr=curr->all_next){
    if((addr >= curr->start) && (addr < curr->start + curr->length)){
      return curr;
    }
  }

  return 0;
}


void* add_memrange(uint32_t domain_id, void* start, uint64_t length){
  // Ensure that all the structure will fit in one page
  ensure_space(sizeof(struct memrange));
  struct memrange* new_memrange = (struct memrange*) numa_allocator.topology_end;

  // Compute next empty place
  numa_allocator.topology_end += sizeof(struct memrange);
  numa_allocator.remaining -= sizeof(struct memrange);

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


void kfree_numa(void* pa){
  struct memrange* memrange = find_memrange(pa);
  if(!memrange){
    printf("Page: %p\n", pa);
    panic("No memory range associated with this page");
  }
  struct domain* d = memrange->domain;
  
  struct run* r = pa;

  acquire(&d->freepages.lock);
  r->next = d->freepages.freelist;
  d->freepages.freelist = r;
  release(&d->freepages.lock);
}


// Returns pointer to a free page inside the given numa domain
void* kalloc_node(struct domain* d){
  struct run* r;

  acquire(&d->freepages.lock);
  r = d->freepages.freelist;
  if(r)
    d->freepages.freelist = r->next;
  release(&d->freepages.lock);

  return r;
}


// Returns a page inside the current domain memory range
// If there is no more free pages in the current domain search in all
void* kalloc_numa(void){
  struct domain* d = my_domain();
  struct run* r;
  
  r = kalloc_node(d);

  // No free page in this domain
  if(!r){
    // Search for first free page in the whole machine
    for(d=machine->all_domains; d; d=d->all_next){
      acquire(&d->freepages.lock);
      r = d->freepages.freelist;
      if(r)
        d->freepages.freelist = r->next;
      release(&d->freepages.lock);

      if(r)
        break;
    }
    printf(
      "freepage %p asked by domain %d, found in domain %d\n",
      r, ((struct domain*)my_domain())->domain_id, d->domain_id
    );
  }

  return r;
}


// Add freepages not included in kernel.freelist and map the associated memory
void add_missing_pages(void){
  struct memrange* m;
  char *r, *memend, *stop;
  unsigned int ctr;

  // Browse all memory ranges
  for(m=machine->all_ranges, ctr=0; m; m=m->all_next, ++ctr){
    // Add all addresses lower than the kernel to the domain freepage list
    r = (char*)PGROUNDUP((uint64)m->start);
    memend = (char*)m->start+m->length;
    stop = ((char*)KERNBASE < memend)? (char*)KERNBASE : memend;

    for(; r + PGSIZE <= stop; r+=PGSIZE){
      // Avoid some specific addresses
      if((r >= (char*)UART0 && r < (char*)UART0+PGSIZE)
      || (r >= (char*)VIRTIO0 && r < (char*)VIRTIO0+PGSIZE)
      || (r >= (char*)PLIC && r < (char*)PLIC+0x400000)){
        continue;
      }
      // Map lower pages
      kvmmap(kernel_pagetable, (uint64)r, (uint64)r, PGSIZE, PTE_R | PTE_W);
      
      // Add the page to the domain freelist
      kfree_numa(r);
    }
    printf("Stopped at %p (> %p), added %d pages\n", r, stop, ctr);


    
    // Add all addresses greater than the kernel to the domain freepage list
    for(r=(char*)PHYSTOP, ctr=0; r + PGSIZE <= memend ; r+=PGSIZE, ++ctr){
      // Avoid some specific addresses
      if(r >= (char*)TRAMPOLINE && r < (char*)TRAMPOLINE+PGSIZE){
        continue;
      }
      // Map upper pages
      kvmmap(kernel_pagetable, (uint64)r, (uint64)r, PGSIZE, PTE_R | PTE_W);

      // Add the page to the domain freelist
      kfree_numa(r);
    }
    printf("Stopped at %p (> %p), added %d pages\n", r, (char*)PHYSTOP, ctr);
  }
}


// Fill the freepage list of each domain by browsing the kernel freepage list
void assign_freepages(){
  struct run *r, *r2;
  struct memrange* memrange;
  struct domain* domain;
  
  // Get the first free page and clear the freelist
  acquire(&kmem.lock);
  r = kmem.freelist;
  kmem.freelist = (void*)0;
  release(&kmem.lock);
  numa_ready = 1;


  // Browse all free pages
  while(r){
    r2 = r->next;

    // Look for a domain containing this page
    memrange = find_memrange(r);
    if(!memrange){
      printf("Page: %p\n", r);
      panic("No memory range associated with this page");
    }
    domain = memrange->domain;

    // Attach freepage to right domain
    acquire(&domain->freepages.lock);
    r->next = domain->freepages.freelist;
    domain->freepages.freelist = r;
    release(&domain->freepages.lock);

    r = r2;
  }

  add_missing_pages();
}


void print_cpu(struct cpu_desc* cpu){
  printf("(%p) CPU id %d\n", cpu, cpu->lapic);
}


void print_memrange(struct memrange* memrange){
  printf(
    "(%p) Memory range: %p -- %p\n",
    memrange, memrange->start, memrange->start + memrange->length
  );
}


// Print the topology of the entire machine
void print_topology(){
  struct domain* curr_dom;
  struct cpu_desc* curr_cpu;
  struct memrange* curr_memrange;
  struct run* curr_pg;
  uint ctr;

  if(!machine){
    return;
  }

  // Browse all domains
  for(curr_dom=machine->all_domains; curr_dom; curr_dom=curr_dom->all_next){
    printf("(%p) Numa domain %p:\n", curr_dom, curr_dom->domain_id);

    // Browse all cpu of a given domain
    for(curr_cpu=curr_dom->cpus; curr_cpu; curr_cpu=curr_cpu->next){
      print_cpu(curr_cpu);
    }

    // Browse all memory ranges of a given domain
    for(curr_memrange=curr_dom->memranges; curr_memrange; curr_memrange=curr_memrange->next){
      print_memrange(curr_memrange);
    }

    // Browse all freepages of a given domain
    for(ctr=0, curr_pg=curr_dom->freepages.freelist; curr_pg; ctr++, curr_pg=curr_pg->next){}
    printf("\t\t     There are %d pages associated with this domain\n", ctr);

    printf("\n");
  }
}