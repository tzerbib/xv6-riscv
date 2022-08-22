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
#define FDT_MMOD_RES        "mmode_resv"
#define FDT_CPUS_NODE       "cpus"
#define FDT_CPU_NODE        "cpu@"
#define FDT_CPU_PROP        "cpu"
#define FDT_MEMORY          "memory@"
#define FDT_NUMA_DOMAIN     "numa-node-id"
#define FDT_RESERVED_MEM    "reserved-memory"
#define FDT_INTERRUPTS      "interrupts"
#define FDT_UART            "uart@"
#define FDT_VIRTIO_MMIO     "virtio_mmio@10001000"
#define FDT_PLIC            "plic@"

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

struct fdt_repr{
  const void* fd_struct;
  const void* fd_struct_end;
  const void* fd_strings;
  const void* dtb_start;
  const void* dtb_end;
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

struct args_print_dt{
  int tab_ctr;
  struct cells* c;
};

struct args_get_node{
  char* node_name;
  unsigned int size;
  const void* addr;
};

struct args_reserved{
  const void* addr;
  unsigned char* reserved;
};


struct args_parse_reg{
  struct cells* c;
  void (*f)(ptr_t, ptr_t, void*);
  void* args;
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


// Skip property content in dtb node
static inline void* skip_fd_prop(const uint32_t* p){
  return (uint32_t*)((char*)p + 8 + bigToLittleEndian32(p+1));
}


// Return address of the next node of same level in dtb or the end of the dtb
static inline const uint32_t* skip_fd_node(const void* node){
  return applySubnodes(node, 0, 0);
}


static inline const uint32_t* get_all_res(const void* node, void* args){
  return applyProperties(node, parse_reg, args);
}