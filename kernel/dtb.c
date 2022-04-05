#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "dtb.h"


void check_dtb(unsigned long pa_dtb){
  const struct fdt_header *fdt = (void*)pa_dtb;

  // Check magic number
  uint32_t magic = bigToLittleEndian32(&fdt->magic);
  if(magic != FDT_MAGIC){
    printf("given %p, expected %p\n", magic, FDT_MAGIC);
    panic("freerange: given address is not pointing to a FDT");
  }
}


// Apply f to all properties.
uint32_t*
applyProperties(void* node, void* dtb_end, void* strings, void (*f)(char*, char*, uint32_t, void*), void* args){
  uint32_t* i = node;

  // Skip node name
  char* c = (char*)(i+1);
  for(; *c; c++);
  i = (uint32_t*)(c+1);
  
  for(; i < (uint32_t*)dtb_end; i++){
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
        char* prop_name = (char*)strings+bigToLittleEndian32(i+2);
                
        f(prop_name, (char*)(i+3), bigToLittleEndian32(i+1), args);
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


// Apply f to all subnodes.
uint32_t*
applySubnodes(void* node, void* dtb_end, void* strings, uint32_t* (*f)(void*, void*), void* args){
  uint32_t* i = node;

  // Skip node name
  char* c = (char*)(i+1);
  for(; *c; c++);
  i = (uint32_t*)(c+1);
  
  for(; i < (uint32_t*)dtb_end; i++){
    // Ensure token 4-byte alignment
    while((ptr_t)i%4) {i=(uint32_t*)((char*)i+1);}

    switch(bigToLittleEndian32(i)){
      case FDT_NOP:
        break;
      
      case FDT_BEGIN_NODE:
        i = f(i, args);
        break;
      
      case FDT_PROP:
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


// Return the number of properties of a given node
int get_nb_prop(void* begin, void* end, void* strings, int nb_parent, struct properties parent_p[]){
  int r = nb_parent;

  // Check if begin points to a node
  if(bigToLittleEndian32(begin) != FDT_BEGIN_NODE){
    panic("in get_address_cells: begin should be the begining of a node");
  }

  // Skip node name
  uint32_t* i = begin+1;
  char* c;
  for(c = (char*)(i+1); *c; c++);
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
        return r;
        break;
      
      case FDT_PROP:
        // Get property name
        char* prop_name = (char*)strings+bigToLittleEndian32(i+2);

        // Check if property already exist in parent
        int np = 0;
        while(np < nb_parent){
          if(!strcmp(prop_name, parent_p[np].name)){
            break;
          }
        }
        
        if(np == nb_parent)
          r++;
        
        i = skip_fd_prop(i);
        break;
      
      case FDT_END_NODE: 
        return r;
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
  return r;
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


void parse_prop(void* node, void* dtb_end, void* strings, struct properties* self, struct properties* parent, int nb_parent){
  if(parent)
    memmove(self, parent, nb_parent*sizeof(struct properties));

  struct args_parse_prop args;
  args.nb_parent = nb_parent;
  args.self = self;
  args.nb_prop = nb_parent;

  applyProperties(node, dtb_end, strings, add_new_prop, &args);
}



// If the node has a property named prop, then returns 1 and set buf to property
// Else return 0.
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
        // DTB specifies that all properties of a node preceide subnodes
        return 0;
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


static inline void pretty_spacing(int ctr){
  for(int i=0; i<ctr; i++){
    printf("\t");
  }
}


void print_property(char* prop_name, char* value, uint32_t size, void* param){
  struct args_print_dt_prop* args = param;

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

  // Case phadle
  else if(!memcmp(prop_name, FDT_PHANDLE, sizeof(FDT_PHANDLE))){
    printf(": %d", bigToLittleEndian32((uint32_t*)value));
  }

  // Case reg
  else if(!memcmp(prop_name, FDT_REG, sizeof(FDT_REG))){
    printf(": ");
    printf("[s:%d ac:%d sc:%d r:%d] ", size, args->c->address_cells, args->c->size_cells, size/(4*(args->c->address_cells+args->c->size_cells)));
    for(int i=0; i<size/(4*(args->c->address_cells+args->c->size_cells)); i++){
      printf("< ");
      // Print ac addresses
      for(int j=0; j<args->c->address_cells;j++){
        printf("0x%x ", bigToLittleEndian32((uint32_t*)value+i*(args->c->address_cells+args->c->size_cells)+j));
      }
      printf("; ");
      // Print sc sizes
      for(int j=0; j<args->c->size_cells;j++){
        printf("0x%x ", bigToLittleEndian32((uint32_t*)value+i*(args->c->address_cells+args->c->size_cells)+args->c->address_cells+j));
      }
      printf("> ");
    }
  }

  printf("\n");
}


uint32_t* print_dt_node(void* node, void* param){
  struct args_print_dt_node* args = param;

  // Print node name
  pretty_spacing(args->tab_ctr);
  printf("BEGIN NODE ");
  char* c;
  for(c = (char*)((uint32_t*)node+1); *c; c++) printf("%c", *c);
  printf("\n");
  
  // Print properties
  struct args_print_dt_prop args_print_prop;
  args_print_prop.tab_ctr = args->tab_ctr+1;
  args_print_prop.c = &args->c;
  // args_print_dt_prop.props = props;
  applyProperties(node, args->dtb_end, args->strings, print_property, &args_print_prop);

  struct args_print_dt_node new_args;
  new_args.dtb_end = args->dtb_end;
  new_args.strings = args->strings;
  new_args.tab_ctr = args->tab_ctr+1;
  new_args.c.address_cells = FDT_DFT_ADDR_CELLS;
  new_args.c.size_cells = FDT_DFT_SIZE_CELLS;
  get_prop(node, args->strings, FDT_ADDRESS_CELLS, sizeof(FDT_ADDRESS_CELLS), &new_args.c.address_cells);
  get_prop(node, args->strings, FDT_SIZE_CELLS, sizeof(FDT_SIZE_CELLS), &new_args.c.size_cells);

  uint32_t* r = applySubnodes(node, args->dtb_end, args->strings, print_dt_node, &new_args);

  pretty_spacing(args->tab_ctr);
  printf("END NODE\n");

  return r;
}


void print_dtb(void* pa_dtb){
  const struct fdt_header *fdt = pa_dtb;

  void* fd_struct = (void*)((char*)fdt) + bigToLittleEndian32(&fdt->off_dt_struct);
  void* fd_strings = (void*)((char*)fdt + bigToLittleEndian32(&fdt->off_dt_strings));
  void* fd_end = (char*)fd_struct+bigToLittleEndian32(&fdt->size_dt_struct);

  printf("\n");
  struct args_print_dt_node args;
  args.dtb_end = fd_end;
  args.strings = fd_strings;
  args.tab_ctr = 0;
  args.c.address_cells = FDT_DFT_ADDR_CELLS;
  args.c.size_cells = FDT_DFT_SIZE_CELLS;
  print_dt_node(fd_struct, &args);
  printf("\n");
}