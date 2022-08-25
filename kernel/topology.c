#include "topology.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "acpi.h"
#include "dtb.h"
#include <stdint.h>


extern struct kmem kmem;
extern ptr_t ksize;
extern char* p_entry;         // first physical address of kernel.
extern pagetable_t kernel_pagetable;
extern struct fdt_repr fdt;

struct machine* machine;       // Beginning of whole machine structure

struct{
  void* topology_end;          // Next empty place for machine substructures
  uint64_t remaining;          // Remaining size in the last topology page
}numa_allocator;


const void* reserved_node;
uint32_t current_domain;

struct device* uart0;
struct device* virtio0;

struct args_excl_reserved{
  ptr_t* addr;
  ptr_t* range;
  uint32_t* domain;
};

struct dtbkvmmake{
  struct cells* c;
  pagetable_t pgt;
};

struct args_mapdevrange{
  void* start;
  ptr_t* length;
  pagetable_t pgt;
};


// Ensure that there is enought space for a structure to be added
void ensure_space(uint64_t length){
  // Each page contain a pointer to the next one
  char* old_page = numa_allocator.topology_end;

  if(numa_allocator.remaining < length){
    numa_allocator.topology_end = kalloc();

    if(machine){
      // Make old page point to new page
      *((ptr_t*)PGROUNDDOWN((ptr_t)old_page)) = (ptr_t) numa_allocator.topology_end;
    }
    
    // Initialize next page address at NULL
    *((ptr_t*)numa_allocator.topology_end) = 0;
    numa_allocator.topology_end += sizeof(void*);
    numa_allocator.remaining = PGSIZE - sizeof(void*);
  }
}


void init_topology(uint32_t domain){
  current_domain = domain;
  machine = 0;

  numa_allocator.topology_end = 0;
  numa_allocator.remaining = 0;

  ensure_space(sizeof(struct machine));
  machine = (struct machine*) numa_allocator.topology_end;
  machine->all_cpus = 0;
  machine->all_ranges = 0;
  machine->all_domains = 0;
  machine->all_devices = 0;

  // Point next empty space on newly allocated area
  numa_allocator.topology_end += sizeof(struct machine);
  numa_allocator.remaining -= sizeof(struct machine);
}


struct domain* add_domain(uint32_t id){
  // Ensure that all the structure will fit in one page
  ensure_space(sizeof(struct domain));
  struct domain* new_domain = (struct domain*) numa_allocator.topology_end;

  // Fill the structure information
  new_domain->domain_id = id;
  new_domain->memranges = 0;
  new_domain->cpus = 0;
  new_domain->devices = 0;
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
struct domain* find_domain(struct machine* m, uint32_t id, char create){
  struct domain* curr;
  
  // Browse all domains until finding the good one
  for(curr=m->all_domains; curr; curr=curr->all_next){
    if(curr->domain_id == id){
      return curr;
    }
  }

  // Create new domain
  return create ? add_domain(id) : (void*)0;
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
  struct domain* d = find_domain(machine, domain_id, 1);
  new_cpu->domain = d;
  new_cpu->next = d->cpus;
  d->cpus = new_cpu;

  // Link this new structure to the others
  new_cpu->all_next = machine->all_cpus;
  machine->all_cpus = new_cpu;

  return new_cpu;
}


void* find_memrange(struct machine* m, void* addr){
  if(!m)
    panic("in find_memrange: machine is NULL");

  struct memrange* curr = m->all_ranges;

  for(; curr; curr=curr->all_next){
    if((addr >= curr->start) && (addr < curr->start + curr->length)){
      return curr;
    }
  }

  return 0;
}


void* add_memrange(uint32_t domain_id, void* start, uint64_t length, uint8_t reserved){
  // Ensure that all the structure will fit in one page
  ensure_space(sizeof(struct memrange));
  struct memrange* new_memrange = (struct memrange*) numa_allocator.topology_end;

  // Compute next empty place
  numa_allocator.topology_end += sizeof(struct memrange);
  numa_allocator.remaining -= sizeof(struct memrange);

  // Fill the structure information
  new_memrange->start = start;
  new_memrange->length = length;
  new_memrange->reserved = reserved;
  struct domain* d = find_domain(machine, domain_id, 1);
  new_memrange->domain = d;
  new_memrange->next = d->memranges;
  d->memranges = new_memrange;

  // Link this new structure to the others
  new_memrange->all_next = machine->all_ranges;
  machine->all_ranges = new_memrange;
  
  return new_memrange;
}


void* add_device(uint32_t id, uint32_t owner, uint32_t irq, void* start, uint64_t length){
  // Ensure that all the structure will fit in one page
  ensure_space(sizeof(struct device));
  struct device* new_dev = (struct device*) numa_allocator.topology_end;

  // Compute next empty place
  numa_allocator.topology_end += sizeof(struct device);
  numa_allocator.remaining -= sizeof(struct device);
  
  // Fill the structure information
  new_dev->id = id;
  new_dev->irq = irq;
  new_dev->start = start;
  new_dev->length = length;
  struct domain* d = find_domain(machine, owner, 0);
  new_dev->owner = d;
  new_dev->next = d->devices;
  d->devices = new_dev;

  // Link this new structure to the others
  new_dev->all_next = machine->all_devices;
  machine->all_devices = new_dev;

  return new_dev;
}


void __freerange(ptr_t addr, ptr_t range, void* param){
  unsigned int ctr = 0;

  char *p = (char*)PGROUNDDOWN(addr);
  for(; p + PGSIZE <= (char*)addr+range; p += PGSIZE){
    // Avoid freeing the kernel and OpenSBI
    if(((p >= (char*)PGROUNDDOWN((ptr_t)p_entry) && p < (char*)PGROUNDUP((ptr_t)(p_entry+ksize)))) || is_reserved(reserved_node, (ptr_t)p))
      continue;

    // Avoid DTB
    if(p >= (char*)PGROUNDDOWN((ptr_t)fdt.dtb_start)
    && p < (char*)PGROUNDUP((ptr_t)fdt.dtb_end))
      continue;

    ++ctr;
    kfree(p);
  }
  // printf("%p -- %p, added %d pages\n", PGROUNDDOWN(addr), p, ctr);
}



const uint32_t*
get_current_domain(const void* node, void* param){
  uint32_t cpu;

  // Check only "cpu@" nodes
  if(memcmp((uint32_t*)node+1, FDT_CPU_NODE, sizeof(FDT_CPU_NODE)-1))
    return skip_fd_node(node);

  // Get cpu id and compare with current cpu
  if(!get_prop(node, FDT_REG, sizeof(FDT_REG), &cpu))
    panic("In get_current_domain, no cpu id");
  
  if(cpu != cpuid())
    return skip_fd_node(node);

  // Store domain of current cpu
  get_prop(node, FDT_NUMA_DOMAIN, sizeof(FDT_NUMA_DOMAIN), &current_domain);

  return fdt.fd_struct_end;
}


// Parse the DTB and allocate memory ranges according to "memory@<addr>" nodes.
const uint32_t*
freerange_node(const void* node, void* param){
  // Set #address-cells and #size-cells for children
  struct cells c = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};
  get_prop(node, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS), &c.address_cells);
  get_prop(node, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS), &c.size_cells);
  
  // If node name starts with "memory@", freerange
  char* name = (char*)((uint32_t*)node+1);
  if(!memcmp(name, FDT_MEMORY, sizeof(FDT_MEMORY)-1)){
    struct args_parse_reg args_reg;
    
    uint32_t numa_node;
    get_prop(node, FDT_NUMA_DOMAIN, sizeof(FDT_NUMA_DOMAIN), &numa_node);

    if(numa_node == current_domain){
      args_reg.c = param;
      args_reg.f = __freerange;

      applyProperties(node, parse_reg, &args_reg);
    }
  }

  const uint32_t* next = applySubnodes(node, freerange_node, &c);
  return next;
}


void
freerange(void)
{
  struct cells cell = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};

  // Get reserved node (i.e. do not free OpenSBI)
  reserved_node = get_node(FDT_RESERVED_MEM, sizeof(FDT_RESERVED_MEM));

  // Get domain of currently running cpu
  const void* node_cpus = get_node(FDT_CPUS_NODE, sizeof(FDT_CPUS_NODE));
  applySubnodes(node_cpus, get_current_domain, 0);

  freerange_node(fdt.fd_struct, &cell);
}



void mapdevrange(ptr_t addr, ptr_t range, void* param){
  struct args_mapdevrange* args = param;

  // Store the address and length of device address space
  *((ptr_t*)args->start) = addr;
  *((ptr_t*)args->length) = range;

  kvmmap(args->pgt, addr, addr, range, PTE_R | PTE_W);
}


void mapdev(const void* node, char* name, struct cells* cell, void* args_mpdvrg, uint32_t* irq){
  struct args_parse_reg args_reg;
  args_reg.c = cell;
  args_reg.args = args_mpdvrg;
  args_reg.f = mapdevrange;

  get_prop(node, FDT_INTERRUPTS, sizeof(FDT_INTERRUPTS), irq);
  
  applyProperties(node, parse_reg, &args_reg);
}


uint32_t get_dev_owner(const void* node, void* start){
  uint32_t owner = DOM_DEV_DFT;
  struct domain* d;
  struct memrange* mr;

  // Check if a domain is specified in the DTB
  // Otherwise try by checking the starting address of the device
  if(!get_prop(node, FDT_NUMA_DOMAIN, sizeof(FDT_NUMA_DOMAIN), &owner))
    if((mr = find_memrange(machine, start)))
       if((d = mr->domain))
        owner = d->domain_id;

  return owner;
}


const uint32_t*
__dtb_kvmmake(const void* node, void* param){
  struct dtbkvmmake* args = param;

  struct cells new_cells = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};
  get_prop(node, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS), &new_cells.address_cells);
  get_prop(node, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS), &new_cells.size_cells);

  char* c = (char*)((uint32_t*)node+1);

  struct args_mapdevrange dest_args;
  dest_args.pgt = args->pgt;
  uint32_t irq = 0;
  uint32_t owner;
  void* start = 0;
  ptr_t length = 0;
  dest_args.start = &start;
  dest_args.length = &length;

  // Case node name starts with "uart@"
  if(!memcmp(c, FDT_UART, sizeof(FDT_UART)-1)){
    mapdev(node, c, args->c, &dest_args, &irq);
    owner = get_dev_owner(node, start);
    uart0 = add_device(ID_UART, owner, irq, start, length);
  }

  // Case node name starts with "virtio_mmio@10001000"
  if(!memcmp(c, FDT_VIRTIO_MMIO, sizeof(FDT_VIRTIO_MMIO)-1)){
    mapdev(node, c, args->c, &dest_args, &irq);
    owner = get_dev_owner(node, start);
    virtio0 = add_device(ID_DISK, owner, irq, start, length);
  }

  // Case node name starts with "plic@"
  if(!memcmp(c, FDT_PLIC, sizeof(FDT_PLIC)-1)){
    mapdev(node, c, args->c, &dest_args, &irq);
    owner = get_dev_owner(node, start);
    add_device(ID_PLIC, owner, irq, start, length);
  }

  // Case node name starts with "clint@"
  if(!memcmp(c, FDT_CLINT, sizeof(FDT_CLINT)-1)){
    mapdev(node, c, args->c, &dest_args, &irq);
    owner = get_dev_owner(node, start);
    add_device(ID_CLINT, owner, irq, start, length);
  }

  struct cells* tmp_cell = args->c;
  args->c = &new_cells;
  const uint32_t* next = applySubnodes(node, __dtb_kvmmake, args);
  args->c = tmp_cell;
  return next;
}


// Add the UART, VIRTIO and PLIC to the kernel pagetable
void dtb_kvmmake(void* kpgtbl){
  struct dtbkvmmake args;
  struct cells cell = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};
  args.c = &cell;
  args.pgt = kpgtbl;
  __dtb_kvmmake(fdt.fd_struct, &args);
}



void __exclude_reserved(ptr_t addr, ptr_t range, void* param){
  struct args_excl_reserved* args = param;
  void compute_ranges(ptr_t, ptr_t, void*);
  ptr_t r_addr = 0;
  ptr_t r_range = 0;

  // Case reserved memory in between the border of the tested memory range
  if(*args->addr < addr && *args->addr+*args->range > addr+range){
    // Treat end of memory range separately
    compute_ranges(addr+range, *args->addr+*args->range-(addr+range), args->domain);
    *args->range = addr+range - *args->addr;
  }

  // Case start of the memrange in a reserved area
  if(*args->addr >= addr && *args->addr < addr+range){
    r_addr = *args->addr;
    r_range = (addr+range < *args->addr+*args->range)? addr+range - r_addr : *args->addr+range;
    *args->range = ((addr+range) - r_addr < *args->range)? *args->range - ((addr+range) - r_addr) : 0;
    *args->addr = addr+range;
  }

  // Case end of the memrange in a reserved area
  else if(*args->addr+*args->range > addr && *args->addr+*args->range <= addr+range){
    r_addr = addr;
    r_range = *args->addr + *args->range - addr;
    *args->range = addr - *args->addr;
  }

  // Allocate the reserved memory range
  if(r_range){
    add_memrange(*args->domain, (void*)r_addr, r_range, 1);
    kvmmap(kernel_pagetable, r_addr, r_addr, r_range, PTE_R | PTE_X);
  }
}


void exclude_reserved(ptr_t* addr, ptr_t* range, uint32_t domain){
  if(!reserved_node)
    return;

  struct cells c = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};
  get_prop(reserved_node, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS), &c.address_cells);
  get_prop(reserved_node, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS), &c.size_cells);

  struct args_excl_reserved args;
  struct args_parse_reg args_reg;
  args.addr = addr;
  args.range = range;
  args.domain = &domain;
  args_reg.c = &c;
  args_reg.args = &args;
  args_reg.f = __exclude_reserved;
  
  applySubnodes(reserved_node, get_all_res, &args_reg);
}


void compute_ranges(ptr_t addr, ptr_t range, void* param){
  uint32_t* domain = param;

  // Avoid reserved sections
  exclude_reserved(&addr, &range, *domain);

  // Avoid DTB pages
  struct args_excl_reserved args;
  args.addr = &addr;
  args.range = &range;
  args.domain = domain;
  __exclude_reserved(PGROUNDDOWN((ptr_t)fdt.dtb_start), PGROUNDUP((ptr_t)fdt.dtb_end)-PGROUNDDOWN((ptr_t)fdt.dtb_start), &args);

  add_memrange(*domain, (void*)addr, range, 0);
  kvmmap(kernel_pagetable, addr, addr, range, PTE_R | PTE_W);
}


const uint32_t*
compute_topology(const void* node, void* param){
  struct cells* cell = param;

  struct cells new_cells = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};
  get_prop(node, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS), &new_cells.address_cells);
  get_prop(node, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS), &new_cells.size_cells);

  char* c = (char*)((uint32_t*)node+1);

  // Case node name starts with "cpu@"
  if(!memcmp(c, FDT_CPU_NODE, sizeof(FDT_CPU_NODE)-1)){
    uint32_t domain, id;

    if(!get_prop(node, FDT_NUMA_DOMAIN, sizeof(FDT_NUMA_DOMAIN), &domain)){
      printf("In node %s (%p), no domain-id for cpu\n", c, node);
      panic("");
    }
    
    if(!get_prop(node, FDT_REG, sizeof(FDT_REG), &id)){
      printf("In node %s (%p), no cpu id\n", c, node);
      panic("");
    }

    add_cpu(domain, id);
  }

  // Case node name starts with "memory@"
  if(!memcmp(c, FDT_MEMORY, sizeof(FDT_MEMORY)-1)){
    uint32_t domain;
    struct args_parse_reg args_reg;
    args_reg.args = &domain;
    args_reg.c = cell;
    args_reg.f = compute_ranges;

    if(!get_prop(node, FDT_NUMA_DOMAIN, sizeof(FDT_NUMA_DOMAIN), &domain)){
      printf("In node %s (%p), no domain-id for memory\n", c, node);
      panic("");
    }

    applyProperties(node, parse_reg, &args_reg);
  }

  const uint32_t* next = applySubnodes(node, compute_topology, &new_cells);
  return next;
}


// Add a numa topology given by the DTB to the machine description
void add_numa(){
  struct cells cell = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};
  compute_topology(fdt.fd_struct, &cell);
}


void kfree_numa(void* pa){
  struct memrange* memrange = find_memrange(machine, pa);
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

    // Print when a memory allocation is performed in another domain
    // printf(
    //   "freepage %p asked by domain %d, found in domain %d\n",
    //   r, ((struct domain*)my_domain())->domain_id, d->domain_id
    // );
  }

  return r;
}


// Fill the freepage list of each domain by browsing the kernel freepage list
void assign_freepages(void* pa_dtb){
  struct run *r, *r2;
  struct memrange* memrange;
  struct domain* domain;
  unsigned int ctr = 0;
  
  // Get the first free page and clear the freelist
  acquire(&kmem.lock);
  r = kmem.freelist;
  kmem.freelist = (void*)0;
  release(&kmem.lock);

  // Browse all free pages
  while(r){
    r2 = r->next;

    // Look for a domain containing this page
    memrange = find_memrange(machine, r);
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
    ++ctr;
  }

  // printf("From xv6 freelist, extracted %d pages\n", ctr);
}


struct domain* get_domain(int cpu){
  struct cpu_desc* curr;
  // Browse cpu structures until the one running this code is found
  for(curr=machine->all_cpus; curr; curr=curr->all_next){
    if(curr->lapic == cpu){
      return curr->domain;
    }
  }

  return 0;
}


void forall_domain(void (*f)(void*, void*), void* args){
  struct domain* curr_dom;

  // Browse all domains
  for(curr_dom=machine->all_domains; curr_dom; curr_dom=curr_dom->all_next){
    f(curr_dom, args);
  }
}

void forall_cpu(void (*f)(void*, void*), void* args){
  struct cpu_desc* curr_cpu;

  // Browse all cpus
  for(curr_cpu=machine->all_cpus; curr_cpu; curr_cpu=curr_cpu->all_next){
    f(curr_cpu, args);
  }
}

void forall_memrange(void (*f)(void*, void*), void* args){
  struct memrange* curr_mr;

  // Browse all cpus
  for(curr_mr=machine->all_ranges; curr_mr; curr_mr=curr_mr->all_next){
    f(curr_mr, args);
  }
}


void forall_cpu_in_domain(struct domain* d, void (*f)(void*, void*), void* args){
  struct cpu_desc* curr_cpu;

  // Browse all cpus
  for(curr_cpu=d->cpus; curr_cpu; curr_cpu=curr_cpu->next){
    f(curr_cpu, args);
  }
}


static inline void count(void* v, void* args){
  int* n = args;

  *n = *n+1;
}

int get_nb_domain(void){
  int n = 0;

  forall_domain(count, &n);

  return n;
}

int get_nb_cpu(void){
  int n = 0;

  forall_cpu(count, &n);

  return n;
}


int get_nb_cpu_in_domain(struct domain* d){
  int n = 0;
  struct cpu_desc* curr_cpu;

  // Browse all cpu of a given domain
  for(curr_cpu=d->cpus; curr_cpu; curr_cpu=curr_cpu->next){
    ++n;
  }

  return n;
}




void print_cpu(struct cpu_desc* cpu){
  printf("(%p) CPU id %d\n", cpu, cpu->lapic);
}


void print_memrange(struct memrange* memrange){
  printf("(%p) Memory range ", memrange);
  if(memrange->reserved)
    printf("(resv)");
  else
    printf("      ");
  printf(": %p -- %p\n", memrange->start, memrange->start + memrange->length);
}


void print_device(struct device* dev){
  switch((int)dev->id){
  case ID_UART:
    printf("\n(%p) UART0 (%d):", dev, dev->irq);
    break;
  case ID_DISK:
    printf("\n(%p) DISK   (%d):", dev, dev->irq);
    break;
  case ID_PLIC:
    printf("\n(%p) PLIC      :", dev);
    break;
  case ID_CLINT:
    printf("\n(%p) CLINT     :", dev);
    break;
  }
  printf(" %p -- %p", dev->start, dev->start+dev->length);
}


// Print the topology of the entire machine
void print_topology(){
  struct domain* curr_dom;
  struct cpu_desc* curr_cpu;
  struct memrange* curr_memrange;
  struct run* curr_pg;
  struct device* curr_dev;
  uint ctr;

  if(!machine){
    return;
  }

  printf("\n\n--- Machine topology: ---\n\n");

  // Browse all domains
  for(curr_dom=machine->all_domains; curr_dom; curr_dom=curr_dom->all_next){
    printf("(%p) Numa domain %d:\n", curr_dom, curr_dom->domain_id);

    // Browse all cpu of a given domain
    for(curr_cpu=curr_dom->cpus; curr_cpu; curr_cpu=curr_cpu->next){
      print_cpu(curr_cpu);
    }

    // Browse all memory ranges of a given domain
    for(curr_memrange=curr_dom->memranges; curr_memrange; curr_memrange=curr_memrange->next){
      print_memrange(curr_memrange);
    }

    // Browse all freepages of a given domain
    for(ctr=0, curr_pg=curr_dom->freepages.freelist; curr_pg; ++ctr, curr_pg=curr_pg->next);
    printf("(%p) There are %d free pages associated with this domain\n",
           curr_dom->freepages.freelist, ctr);

    // Browse all devices of a given domain
    printf("(%p) Devices: ", 0x0);
    for(ctr=0,curr_dev=curr_dom->devices; curr_dev; curr_dev=curr_dev->next,++ctr){
      print_device(curr_dev);
    }
    if(!ctr)
      printf("None\n");

    printf("\n\n");
  }
}


void print_struct_machine_loc(){
  ptr_t* next = (ptr_t*) PGROUNDDOWN((ptr_t)machine);
  uint32_t domain;

  printf("Topology structure in memory:\n");
  printf("  start");

  while(next){
    domain = ((struct memrange*)find_memrange(machine, next))->domain->domain_id;
    printf(" -> %p (%d)", next, domain);
    next = (ptr_t*)(*next);
  }
  printf(" -> 0\n");
}


void free_machine(){
  ptr_t* next = (ptr_t*) PGROUNDDOWN((ptr_t)machine);
  ptr_t* tmp;
  unsigned int ctr = 0;

  while(next){
    tmp = (ptr_t*)(*next);
    kfree(next);
    next = tmp;
    ctr++;
  }

  machine = 0;

  printf("From old internal struct, freed       %d pages\n", ctr);
}