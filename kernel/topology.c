#include "topology.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "acpi.h"
#include <stdint.h>


extern struct kmem kmem;
extern char end[];             // first address after kernel.
extern pagetable_t kernel_pagetable;
extern char numa_ready;


struct machine* machine = 0;   // Beginning of whole machine structure
struct machine* wip_machine = 0;   // Machine structure while in construction
struct machine* old_machine = 0;   // Old Machine structure not yet freed

struct {
  void* topology_end;          // Next empty place for machine substructures
  uint64_t remaining;          // Remaining size in the last topology page
}numa_allocator;


#define FDT_MAGIC           0xd00dfeed
#define FDT_BEGIN_NODE      0x00000001
#define FDT_END_NODE        0x00000002
#define FDT_PROP            0x00000003
#define FDT_NOP             0x00000004
#define FDT_END             0x00000009

#define FDT_ADDRESS_CELLS   "#address-cells"
#define FDT_DFT_ADDR_CELLS  2
#define FDT_SIZE_CELLS      "#size-cells"
#define FDT_DFT_SIZE_CELLS  1
#define FDT_PHANDLE         "phandle"
#define FDT_REG             "reg"
#define FDT_MEMORY          "memory@"

struct fdt_header {
  uint32_t magic;
  uint32_t totalsize;
  uint32_t off_dt_struct;
  uint32_t off_dt_strings;
  uint32_t off_mem_rsvmap;
  uint32_t version;
  uint32_t last_comp_version;
  uint32_t boot_cpuid_phys;
  uint32_t size_dt_strings;
  uint32_t size_dt_struct;
};


// Ensure that there is enought space for a structure to be added
void ensure_space(uint64_t length){
  // Each page contain a pointer to the next one
  char* old_page = numa_allocator.topology_end;

  if(numa_allocator.remaining < length){
    numa_allocator.topology_end = kalloc();

    if(wip_machine){
      // Make old page point to new page
      *((ptr_t*)PGROUNDDOWN((ptr_t)old_page)) = (ptr_t) numa_allocator.topology_end;
    }
    
    // Initialize next page address at NULL
    *((ptr_t*)numa_allocator.topology_end) = 0;
    numa_allocator.topology_end += sizeof(void*);
    numa_allocator.remaining = PGSIZE - sizeof(void*);
  }
}


void init_topology(){
  wip_machine = 0;
  numa_allocator.topology_end = 0;
  numa_allocator.remaining = 0;

  ensure_space(sizeof(struct machine));
  wip_machine = (struct machine*) numa_allocator.topology_end;
  wip_machine->all_cpus = 0;
  wip_machine->all_ranges = 0;
  wip_machine->all_domains = 0;

  // Point next empty space on newly allocated area
  numa_allocator.topology_end += sizeof(struct machine);
  numa_allocator.remaining -= sizeof(struct machine);
}


void finalize_topology(){
  old_machine = machine;
  machine = wip_machine;
  numa_ready = 1;  // switch to kalloc_numa
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
  new_domain->all_next = wip_machine->all_domains;
  wip_machine->all_domains = new_domain;

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
  struct domain* d = find_domain(wip_machine, domain_id, 1);
  new_cpu->domain = d;
  new_cpu->next = d->cpus;
  d->cpus = new_cpu;

  // Link this new structure to the others
  new_cpu->all_next = wip_machine->all_cpus;
  wip_machine->all_cpus = new_cpu;

  return new_cpu;
}


void* find_memrange(struct machine* m, void* addr){
  struct memrange* curr = m->all_ranges;

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
  struct domain* d = find_domain(wip_machine, domain_id, 1);
  new_memrange->domain = d;
  new_memrange->next = d->memranges;
  d->memranges = new_memrange;

  // Link this new structure to the others
  new_memrange->all_next = wip_machine->all_ranges;
  wip_machine->all_ranges = new_memrange;
  
  return new_memrange;
}



// Return the value in big endian of a pointer to a 32 bit value in little endian.
static inline uint32_t bigToLittleEndian32(const uint32_t *p)
{
  const uint8_t *bp = (const uint8_t *)p;

  return ((uint32_t)bp[0] << 24)
    | ((uint32_t)bp[1] << 16)
    | ((uint32_t)bp[2] << 8)
    | bp[3];
}


static inline void* skip_fd_prop(uint32_t* p){
  // Skip property content
  return (uint32_t*)((char*)p + 8 + bigToLittleEndian32(p+1));
}


// Return the address of the corresponding FDT_END_NODE token
void* skip_fd_node(uint32_t* i){
  // Skip node name
  char* c;
  for(c = (char*)(i+1); *c; c++);
  
  // Align on a 4-byte boundary
  i = (uint32_t*)(c+1);

  for(;; i++){
    // Ensure alignment
    while((ptr_t)i%4) {i=(uint32_t*)((char*)i+1);}

    switch(bigToLittleEndian32(i)){
      case FDT_NOP:
        continue;
        break;
      
      case FDT_BEGIN_NODE:
        // Skip lower nodes
        i = skip_fd_node(i);
        break;
      
      case FDT_PROP:
        i = skip_fd_prop(i);
        break;
      
      case FDT_END_NODE: 
        // Specified default value for #address-cells
        return i;
        break;

      case FDT_END:
        panic("in skip_fd_node: reached end of FDT");
        break;
      
      default: {
        printf("in skip_fd_node, token %p not found\n", bigToLittleEndian32(i));
        panic("");
      }
    }
  }
}


// Return the property value of the given node
// begin should point to the begining of a node
char get_prop(void* begin, void* strings, char* prop, uint size, uint32_t* buf){
  // Check if begin points to a node
  if(bigToLittleEndian32(begin) != FDT_BEGIN_NODE){
    panic("in get_address_cells: begin should be the begining of a node");
  }

  // Skip node name
  uint32_t* i = begin+1;
  char* c;
  for(c = (char*)(i+1); *c; c++);

  // Align on a 4-byte boundary
  i = (uint32_t*)(c+1);

  for(;; i++){
    // Ensure alignment
    while((ptr_t)i%4) {i=(uint32_t*)((char*)i+1);}

    switch(bigToLittleEndian32(i)){
      case FDT_NOP:
        continue;
        break;
      
      case FDT_BEGIN_NODE:
        // Skip lower nodes
        i = skip_fd_node(i);
        break;
      
      case FDT_PROP:
        // Print property name
        uint32_t* prop_name = (uint32_t*)((char*)strings+bigToLittleEndian32(i+2));

        // Case prop_name == prop
        if(!memcmp(prop_name, prop, size)){
          *buf = bigToLittleEndian32(i+3);
          return 1;
        }
        
        i = skip_fd_prop(i);
        break;
      
      case FDT_END_NODE: 
        // Specified default value for #address-cells
        return 0;
        break;

      case FDT_END:
        panic("in get_address_cells: reached end of FDT");
        break;
      
      default: {
        printf("in get_address_cells, token %p not found\n", bigToLittleEndian32(i));
        panic("");
      }
    }
  }
}


// Print a string contained in the dtb
static inline void print_fd_string(char* s){
  while(*s){
    printf("%c", *s++);
  }
}


static inline void pretty_spacing(int ctr){
  for(int i=0; i<ctr; i++){
    printf("\t");
  }
}


// Print content of some specific properties
void print_interesting_prop(uint32_t* prop, uint32_t* prop_name, uint32_t ac, uint32_t sc){
  // Case #address-cells
  if(!memcmp(prop_name, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS))){
    printf(": %d cells", bigToLittleEndian32(prop+2));
  }

  // Case #size-cells
  else if(!memcmp(prop_name, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS))){
    printf(": %d cells", bigToLittleEndian32(prop+2));
  }

  // Case phadle
  else if(!memcmp(prop_name, FDT_PHANDLE, sizeof(FDT_PHANDLE))){
    printf(": %d", bigToLittleEndian32(prop+2));
  }

  // Case reg
  else if(!memcmp(prop_name, FDT_REG, sizeof(FDT_REG))){
    printf(": ");
    int size = bigToLittleEndian32(prop);
    printf(" [s:%d ac:%d sc:%d r:%d] ", size, ac, sc, size/(4*(ac+sc)));
    for(int i=0; i<size/(4*(ac+sc)); i++){
      printf("< ");
      // Print ac addresses
      for(int j=0; j<ac;j++){
        printf("0x%x ", bigToLittleEndian32(prop+2+i*(ac+sc)+j));
      }
      printf("; ");
      // Print sc sizes
      for(int j=0; j<sc;j++){
        printf("0x%x ", bigToLittleEndian32(prop+2+i*(ac+sc)+ac+j));
      }
      printf("> ");
    }
    printf("\n");
  }
}


uint32_t* print_dt_node(void* start, void* end, void* strings, uint tab_ctr, int ac, int sc){
  // Set #address-cells and #size-cells for children
  uint32_t new_ac = FDT_DFT_ADDR_CELLS;
  uint32_t new_sc = FDT_DFT_SIZE_CELLS;
  get_prop(start, strings, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS), &new_ac);
  get_prop(start, strings, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS), &new_sc);

  uint32_t* i = start;

  // Initial begin node
  pretty_spacing(tab_ctr);
  printf("BEGIN NODE ");

  // Print node name
  char* c;
  for(c = (char*)(i+1); *c; c++) printf("%c", *c);
  printf("\n");
  tab_ctr++;
  
  // Align on a 4-byte boundary
  i = (uint32_t*)(c+1);

  for(; i < (uint32_t*)end; i++){
    // Ensure token 4-byte alignment
    while((ptr_t)i%4) {i=(uint32_t*)((char*)i+1);}

    switch(bigToLittleEndian32(i)){
      case FDT_NOP:
        pretty_spacing(tab_ctr);
        printf("NOP\n");
        break;
      
      case FDT_BEGIN_NODE:
        i = print_dt_node(i, end, strings, tab_ctr, new_ac, new_sc);
        break;
      
      case FDT_PROP:
        pretty_spacing(tab_ctr);
        printf("PROPERTY ");
        
        // Print property name
        uint32_t* prop_name = (uint32_t*)((char*)strings+bigToLittleEndian32(i+2));
        print_fd_string((char*)prop_name);
        
        print_interesting_prop(i+1, prop_name, ac, sc);
        printf("\n");
        
        // Skip property content
        i = skip_fd_prop(i);
        break;
      
      case FDT_END_NODE:
        pretty_spacing(tab_ctr-1);
        printf("END NODE\n");
        return i;

      case FDT_END:
        // Check bt size integrity
        if(i+1 != end){
            printf("i+1 (%p) != end (%p)\n", i+1, end);
            panic("Should be the end of FDT!");
        }
        break;
      
      default: {
        printf("in FDT, token %p not found\n", bigToLittleEndian32(i));
        panic("");
      }
    }
  }

  return end;
}


// Parse the DTB and allocate memory ranges according to "memory@<addr>" nodes.
uint32_t*
__freerange(void* start, void* dtb_start, void* dtb_end, void* strings, uint32_t ac, uint32_t sc){
  // Set #address-cells and #size-cells for children
  uint32_t new_ac = FDT_DFT_ADDR_CELLS;
  uint32_t new_sc = FDT_DFT_SIZE_CELLS;
  get_prop(start, strings, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS), &new_ac);
  get_prop(start, strings, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS), &new_sc);
  
  uint32_t* i = start;
  char allocate = 0;

  // Check node name
  char* c = (char*)(i+1);
  allocate = !memcmp(c, FDT_MEMORY, sizeof(FDT_MEMORY)-1);

  // Skip node name
  for(; *c; c++);
  
  // Align on a 4-byte boundary
  i = (uint32_t*)(c+1);

  for(; i < (uint32_t*)dtb_end; i++){
    // Ensure token 4-byte alignment
    while((ptr_t)i%4) {i=(uint32_t*)((char*)i+1);}

    switch(bigToLittleEndian32(i)){
      case FDT_NOP:
        break;
      
      case FDT_BEGIN_NODE:
        i = __freerange(i, dtb_start, dtb_end, strings, new_ac, new_sc);
        break;
      
      case FDT_PROP:
        // Check property name
        uint32_t* prop_name = (uint32_t*)((char*)strings+bigToLittleEndian32(i+2));
        if(allocate && !memcmp(FDT_REG, prop_name, sizeof(FDT_REG))){
          int size = bigToLittleEndian32(i+1);
          for(int k=0; k<size/(4*(ac+sc)); k++){
            ptr_t addr = 0;
            ptr_t range = 0;
            for(int j=0; j<ac;j++){
              addr |= ((ptr_t)bigToLittleEndian32((i+1)+2+k*(ac+sc)+j)) << 32*(ac-j-1);
            }
            
            for(int j=0; j<sc;j++){
              range |= (ptr_t)(bigToLittleEndian32((i+1)+2+k*(ac+sc)+ac+j)) << 32*(sc-j-1);
            }

            range += addr;

            char *p = (char*)PGROUNDUP(addr);
            for(; p + PGSIZE <= (char*)range; p += PGSIZE){
              // Avoid kernel
              if(p <= end) continue;

              // Avoid DTB
              if(p >= (char*)dtb_start && p < (char*)dtb_end+PGSIZE) continue;

              kfree(p);
            }
          }
        }
        i = skip_fd_prop(i);
        break;
      
      case FDT_END_NODE:
        return i;

      case FDT_END:
        // Check bt size integrity
        if(i+1 != dtb_end){
            printf("i+1 (%p) != end (%p)\n", i+1, dtb_end);
            panic("Should be the end of FDT!");
        }
        break;
      
      default: {
        printf("in FDT, token %p not found\n", bigToLittleEndian32(i));
        panic("");
      }
    }
  }

  return dtb_end;
}


void
freerange(void *pa_dtb)
{
  const struct fdt_header *fdt = pa_dtb;

  // Check magic number
  uint32_t magic = bigToLittleEndian32(&fdt->magic);
  if(magic != FDT_MAGIC){
    printf("given %p, expected %p\n", magic, FDT_MAGIC);
    panic("freerange: given address is not pointing to a FDT");
  }
  printf("FDT found at %p\n", pa_dtb);

  void* fd_struct = (void*)((char*)fdt) + bigToLittleEndian32(&fdt->off_dt_struct);
  void* fd_strings = (void*)((char*)fdt + bigToLittleEndian32(&fdt->off_dt_strings));
  void* fd_end = (char*)fd_struct+bigToLittleEndian32(&fdt->size_dt_struct);

  printf("\n");
  print_dt_node(fd_struct, fd_end, fd_strings, 0, FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS);
  printf("\n");

  __freerange(fd_struct, pa_dtb, fd_end, fd_strings, FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS);
}


// Add a numa topology given by a SRAT table to the machine description
void add_numa(const void* ptr){
  /* The device tree must be at an 8-byte aligned address */
  if ((uintptr_t)ptr & 7)
    panic("FDT is not 8-byte aligned");
  
  printf("DTB is located at %p\n", ptr);

  const struct fdt_header *fdt = ptr;
  printf("Magic:    %p\nReversed: %p\n", fdt->magic, (bigToLittleEndian32(&((const struct fdt_header *)(ptr))->magic)));
  
  printf("\n");
  panic("Everything's fine... I hope");

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
        ptr_t addr = ((uint64_t)memrange->hi_base << 32) + memrange->lo_base;
        ptr_t length = ((uint64_t)memrange->hi_length << 32) + memrange->lo_length;
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


// Add freepages not included in kernel.freelist and map the associated memory
void add_missing_pages(void){
  struct memrange* m;
  char *r, *memend, *stop;
  unsigned int ctr;

  // Browse all memory ranges
  for(m=machine->all_ranges; m; m=m->all_next){
    // Add all addresses lower than the kernel to the domain freepage list
    r = (char*)PGROUNDUP((uint64)m->start);
    memend = (char*)m->start+m->length;
    stop = ((char*)KERNBASE < memend)? (char*)KERNBASE : memend;

    printf("%p", r);
    for(ctr=0; r + PGSIZE <= stop; r+=PGSIZE){
      // Avoid some risc-V specific addresses
      if((r >= (char*)UART0 && r < (char*)UART0+PGSIZE)
      || (r >= (char*)VIRTIO0 && r < (char*)VIRTIO0+PGSIZE)
      || (r >= (char*)PLIC && r < (char*)PLIC+0x400000)){
        continue;
      }

      // Map lower pages
      kvmmap(kernel_pagetable, (uint64)r, (uint64)r, PGSIZE, PTE_R | PTE_W);
      
      // Add the page to the domain freelist
      kfree_numa(r);
      ++ctr;
    }
    printf(" -- %p (< %p), added %d pages\n", memend, stop, ctr);


    // Add all addresses greater than the kernel to the domain freepage list
    printf("%p", m->start);
    for(r=(char*)PHYSTOP, ctr=0; r + PGSIZE <= memend ; r+=PGSIZE){
      // Avoid some specific addresses
      if(r >= (char*)TRAMPOLINE && r < (char*)TRAMPOLINE+PGSIZE){
        continue;
      }



      // *r = 42;
      // printf("*(%p) = %d\n", r, *r);

      // struct domain* curr_dom;
      // struct run *curr_pg;

      // for(curr_dom=machine->all_domains; curr_dom; curr_dom=curr_dom->all_next){
        // printf("%p in domain %d\n", r, curr_dom->domain_id);
      //   curr_pg = curr_dom->freepages.freelist;
      //   while(curr_pg){
      //     // printf("Check %p\n", curr_pg);
      //     if((void*)curr_pg == r){
      //       printf("For page %p\n", curr_pg);
      //       panic("Page freed twice");
      //     }
      //     curr_pg = curr_pg->next;
      //   }
      // }




      // Map upper pages
      kvmmap(kernel_pagetable, (uint64)r, (uint64)r, PGSIZE, PTE_R | PTE_W);

      // Add the page to the domain freelist
      kfree_numa(r);
      ++ctr;
    }
    printf(" -- %p (> %p), added %d pages\n", memend, (char*)PHYSTOP, ctr);
  }
}


// Fill the freepage list of each domain by browsing the kernel freepage list
void assign_freepages(){
  struct run *r, *r2;
  struct memrange* memrange;
  struct domain* domain;
  unsigned int ctr = 0;
  struct domain* curr_dom;
  struct run *curr_pg, *tmp_pg;

  // Get freepages from the old machine structure
  if(old_machine){
    // Browse all domains
    for(curr_dom=old_machine->all_domains; curr_dom; curr_dom=curr_dom->all_next){
      // Browse all freepages of a given domain
      curr_pg=curr_dom->freepages.freelist;
      while(curr_pg){
        tmp_pg = curr_pg->next;
        kfree(curr_pg);
        curr_pg = tmp_pg;
        ++ctr;
      }
    }
    printf("From old numa topology, extracted %d pages\n", ctr);

    return;
  }
  
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

  printf("From xv6 freelist, extracted %d pages\n", ctr);

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

    printf("\n");
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
  ptr_t* next = (ptr_t*) PGROUNDDOWN((ptr_t)old_machine);
  ptr_t* tmp;
  unsigned int ctr = 0;

  while(next){
    tmp = (ptr_t*)(*next);
    kfree(next);
    next = tmp;
    ctr++;
  }

  old_machine = 0;

  printf("From old internal struct, freed       %d pages\n", ctr);
}