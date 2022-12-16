#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "dtb.h"


struct fdt_repr fdt;


void check_dtb(const void* pa_dtb){
  const struct fdt_header *fdt = (void*)pa_dtb;

  /* The device tree must be at an 8-byte aligned address */
  if ((ptr_t)pa_dtb % 8)
    panic("FDT is not 8-byte aligned");

  // Check magic number
  uint32_t magic = bigToLittleEndian32(&fdt->magic);
  if(magic != FDT_MAGIC){
    printf("given %p, expected %p\n", magic, FDT_MAGIC);
    panic("freerange: given address is not pointing to a FDT");
  }
}


void initialize_fdt(const void* pa_dtb){
  check_dtb(pa_dtb);

  const struct fdt_header *fdt_h = pa_dtb;

  fdt.dtb_start = pa_dtb;
  fdt.fd_struct = (void*)((char*)fdt_h) + bigToLittleEndian32(&fdt_h->off_dt_struct);
  fdt.fd_struct_end = (char*)fdt.fd_struct+bigToLittleEndian32(&fdt_h->size_dt_struct);
  fdt.fd_strings = (void*)((char*)fdt_h + bigToLittleEndian32(&fdt_h->off_dt_strings));
  fdt.dtb_end = (char*)pa_dtb + bigToLittleEndian32(&fdt_h->totalsize);
}


// Apply f to all properties.
const uint32_t*
applyProperties(const void* node, void (*f)(char*, char*, uint32_t, void*), void* args){
  const uint32_t* i = node;

  // Skip node name
  char* c = (char*)(i+1);
  for(; *c; c++);
  i = (uint32_t*)(c+1);
  
  for(; i < (uint32_t*)fdt.fd_struct_end; i++){
    // Ensure token 4-byte alignment
    while((ptr_t)i%4) {i=(uint32_t*)((char*)i+1);}

    switch(bigToLittleEndian32(i)){
      case FDT_NOP:
        break;
      
      case FDT_BEGIN_NODE:
        return i;
        break;
      
      case FDT_PROP:
        // Get property name
        char* prop_name = (char*)fdt.fd_strings+bigToLittleEndian32(i+2);
                
        f(prop_name, (char*)(i+3), bigToLittleEndian32(i+1), args);
        i = skip_fd_prop(i);
        break;
      
      case FDT_END_NODE:
        return i;

      case FDT_END:
        // Check bt size integrity
        if(i+1 != fdt.fd_struct_end){
            printf("i+1 (%p) != end (%p)\n", i+1, fdt.fd_struct_end);
            panic("Should be the end of FDT!");
        }
        break;
      
      default: {
        printf("in FDT, token %p not found\n", bigToLittleEndian32(i));
        panic("");
      }
    }
  }

  return fdt.fd_struct_end;
}


// Apply f to all subnodes.
const uint32_t*
applySubnodes(const void* node, const uint32_t* (*f)(const void*, void*), void* args){
  const uint32_t* i = node;

  // Skip node name
  char* c = (char*)(i+1);
  for(; *c; c++);
  i = (uint32_t*)(c+1);
  
  for(; i < (uint32_t*)fdt.fd_struct_end; i++){
    // Ensure token 4-byte alignment
    while((ptr_t)i%4) {i=(uint32_t*)((char*)i+1);}

    switch(bigToLittleEndian32(i)){
      case FDT_NOP:
        break;
      
      case FDT_BEGIN_NODE:
        if(f)
          i = f(i, args);
        // Skip all subnodes nodes
        else
          i = applySubnodes(i, 0, 0);
        break;
      
      case FDT_PROP:
        i = skip_fd_prop(i);
        break;
      
      case FDT_END_NODE:
        return i;

      case FDT_END:
        // Check bt size integrity
        if(i+1 != fdt.fd_struct_end){
            printf("i+1 (%p) != end (%p)\n", i+1, fdt.fd_struct_end);
            panic("Should be the end of FDT!");
        }
        break;
      
      default: {
        printf("in FDT, token %p not found\n", bigToLittleEndian32(i));
        panic("");
      }
    }
  }

  return fdt.fd_struct_end;
}


void add_new_prop(char* prop_name, char* prop_value, uint32_t p_size, void* p){
  struct args_parse_prop* args = p;

  unsigned int i;
  for(i=0; i<args->nb_parent; ++i){
    // Case property already set in parent
    if(!strcmp(prop_name, args->self[i].name)){
      memmove(args->self[i].val, prop_value, p_size);
      break;
    }
  }

  // Case not in parent
  if(i == args->nb_parent){
    args->self[args->nb_prop].size = p_size;
    strcpy(args->self[args->nb_prop].name, prop_name);
    memmove(args->self[args->nb_prop].val, prop_value, p_size);
    args->nb_prop++;
  }
}


void parse_prop(const void* node, struct properties* self, struct properties* parent, int nb_parent){
  if(parent)
    memmove(self, parent, nb_parent*sizeof(struct properties));

  struct args_parse_prop args;
  args.nb_parent = nb_parent;
  args.self = self;
  args.nb_prop = nb_parent;

  applyProperties(node, add_new_prop, &args);
}


const uint32_t*
get_dt_node(const void* node, void* param){
  struct args_get_node* args = param;

  char* c = (char*)((uint32_t*)node+1);

  // Is this the looked for node
  if(!memcmp(c, args->node_name, args->size)){
    args->addr = node;
    return fdt.fd_struct_end;
  }

  return applySubnodes(node, get_dt_node, args);
}


const void* get_node(char* name, unsigned int size){
  struct args_get_node args;
  args.node_name = name;
  args.size = size;
  args.addr = 0;

  get_dt_node(fdt.fd_struct, &args);

  return args.addr;
}


// If the node has a property named prop, then returns 1 and set buf to property
// Else return 0.
// begin should point to the begining of a node
char get_prop(const void* begin, char* prop, uint size, uint32_t* buf){
  // Check if begin points to a node
  if(bigToLittleEndian32(begin) != FDT_BEGIN_NODE){
    panic("in get_address_cells: begin should be the begining of a node");
  }

  // Skip node name
  const uint32_t* i = begin+1;
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
        // DTB specifies that all properties of a node preceide subnodes
        return 0;
        break;
      
      case FDT_PROP:
        // Print property name
        uint32_t* prop_name = (uint32_t*)((char*)fdt.fd_strings+bigToLittleEndian32(i+2));

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


void parse_reg(char* prop_name, char* prop_val, uint32_t size, void* param){
  struct args_parse_reg* args = param;

  // Check if property is "reg"
  if(!memcmp(FDT_REG, prop_name, sizeof(FDT_REG))){
    for(unsigned int k=0; k<size/(4*(args->c->address_cells+args->c->size_cells)); ++k){
      ptr_t addr = 0;
      ptr_t range = 0;
      for(int j=0; j<args->c->address_cells;++j){
        addr |= ((ptr_t)bigToLittleEndian32((uint32_t*)prop_val+k*(args->c->address_cells+args->c->size_cells)+j)) << 32*(args->c->address_cells-j-1);
      }
      
      for(int j=0; j<args->c->size_cells;++j){
        range |= (ptr_t)(bigToLittleEndian32((uint32_t*)prop_val+k*(args->c->address_cells+args->c->size_cells)+args->c->address_cells+j)) << 32*(args->c->size_cells-j-1);
      }

      args->f(addr, range, args->args);
    }
  }
}


static inline void pretty_spacing(int ctr){
  for(int i=0; i<ctr; i++){
    printf("\t");
  }
}


void print_property(char* prop_name, char* value, uint32_t size, void* param){
  struct args_print_dt* args = param;

  pretty_spacing(args->tab_ctr);
  printf("%s", prop_name);

  // Case #address-cells
  if(!memcmp(prop_name, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS))){
    printf(": %d cells", bigToLittleEndian32((uint32_t*)value));
  }

  // Case #size-cells
  else if(!memcmp(prop_name, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS))){
    printf(": %d cells", bigToLittleEndian32((uint32_t*)value));
  }

  // Case phandle
  else if(!memcmp(prop_name, FDT_PHANDLE, sizeof(FDT_PHANDLE))){
    printf(": %d", bigToLittleEndian32((uint32_t*)value));
  }

  // Case interrupts
  else if(!memcmp(prop_name, FDT_INTERRUPTS, sizeof(FDT_INTERRUPTS))){
    printf(": %d", bigToLittleEndian32((uint32_t*)value));
  }

  // Case reg
  else if(!memcmp(prop_name, FDT_REG, sizeof(FDT_REG))){
    printf(": ");
    for(int i=0; i<size/(4*(args->c->address_cells+args->c->size_cells)); i++){
      printf("< ");
      // Print ac addresses
      for(int j=0; j<args->c->address_cells;++j){
        printf("0x%x ", bigToLittleEndian32((uint32_t*)value+i*(args->c->address_cells+args->c->size_cells)+j));
      }
      printf("; ");
      // Print sc sizes
      for(int j=0; j<args->c->size_cells;++j){
        printf("0x%x ", bigToLittleEndian32((uint32_t*)value+i*(args->c->address_cells+args->c->size_cells)+args->c->address_cells+j));
      }
      printf("> ");
    }
  }

  // Case numa-node-id
  else if(!memcmp(prop_name, FDT_NUMA_DOMAIN, sizeof(FDT_NUMA_DOMAIN))){
    printf(": %d", bigToLittleEndian32((uint32_t*)value));
  }

  // Case cpu
  else if(!memcmp(prop_name, FDT_CPU_PROP, sizeof(FDT_CPU_PROP))){
    printf(": %d", bigToLittleEndian32((uint32_t*)value));
  }

  printf("\n");
}


const uint32_t* print_dt_node(const void* node, void* param){
  struct args_print_dt* args = param;

  // Print node name
  pretty_spacing(args->tab_ctr);
  printf("BEGIN NODE ");
  char* c;
  for(c = (char*)((uint32_t*)node+1); *c; c++) printf("%c", *c);
  printf("\n");
  
  // Print properties
  args->tab_ctr++;
  applyProperties(node, print_property, args);

  struct cells new_cell = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};
  get_prop(node, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS), &new_cell.address_cells);
  get_prop(node, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS), &new_cell.size_cells);


  struct cells* old_cell = args->c;
  args->c = &new_cell;
  const uint32_t* r = applySubnodes(node, print_dt_node, args);
  args->c = old_cell;

  args->tab_ctr--;
  pretty_spacing(args->tab_ctr);
  printf("END NODE\n");

  return r;
}


void print_dtb(){
  printf("\n");
  struct args_print_dt args;
  args.tab_ctr = 0;
  struct cells c = {FDT_DFT_ADDR_CELLS, FDT_DFT_SIZE_CELLS};
  args.c = &c;
  print_dt_node(fdt.fd_struct, &args);
  printf("\n");
}
