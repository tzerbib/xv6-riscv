#include <stdint.h>

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

struct fdt_header{
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


struct cells{
  uint32_t address_cells;
  uint32_t size_cells;
};

struct properties{
  uint32_t size;
  char name[20];
  char val[20];
};

struct args_parse_prop{
  struct properties* self;
  int nb_parent;
  int nb_prop;
};

struct args_print_dt_node{
  void* dtb_end;
  void* strings;
  int tab_ctr;
  struct cells c;
};

struct args_print_dt_prop{
  int tab_ctr;
  struct cells* c;
};


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
