#ifndef __TOPOLOGY__
#define __TOPOLOGY__

#include "types.h"
#include "spinlock.h"
#include "kalloc.h"
#include "communication.h"

#define DOM_DEV_DFT 0

struct machine{
  struct cpu_desc* all_cpus;   // Linked list of all cpu in the machine
  struct memrange* all_ranges; // Linked list of memory ranges of the machine
  struct domain* all_domains;  // Linked list of all numa domains of the machine
  struct device* all_devices;  // Linked list of all devices of the machine
};


struct cpu_desc{
  struct cpu_desc* all_next;   // Next cpu in the list of all cpus
  struct cpu_desc* next;       // Next cpu on the same domain
  struct domain* domain;       // Domain of the cpu
  uint32_t lapic;              // APIC ID
};


struct memrange{
  void* start;                 // Virtual starting address of the memory range
  ptr_t length;                // Length of the memory range
  uint8_t reserved;            // Is this memory region reserved (i.e. firmware)
  struct domain* domain;       // Domain associated with this memory range
  struct memrange* next;       // Next memory range on the same domain
  struct memrange* all_next;   // Next memory range in the global list
};


struct domain{
  uint32_t domain_id;          // Proximity domain (low and high bits)
  struct domain* all_next;     // Next numa domain in the list of all domains
  struct memrange* memranges;  // First memory range of this numa domain
  struct cpu_desc* cpus;       // First cpu of this numa domain 
  struct kmem freepages;       // First free page for this domain
  struct device* devices;      // First device owned by this domain
  struct memrange* kernelmr;   // Memory range containing the kernel text+data
  struct ring* combuf;         // The communication rinbuffer of this domain
};


enum devices_id{
  ID_UART,
  ID_DISK,
  ID_PLIC,
  ID_CLINT
};

struct device{
  struct device* all_next;     // Next device in the list of all devices
  struct device* next;         // Next device on the same domain
  enum devices_id id;          // Device type
  struct domain* owner;              // Domain in charge of this device
  uint32_t irq;                // Interrupt request number for this device
  void* start;                 // Virtual starting address of the device
  ptr_t length;                // Length of the address space of the device
};


void init_topology(uint32_t);
void add_numa(struct machine*);
void print_topology(void);
void assign_freepages(void*);
void* kalloc_node(struct domain* d);
void free_machine(void);
void print_struct_machine_loc(void);
void* find_memrange(struct machine*, void*);

struct domain* get_domain(int);
void forall_domain(struct machine*, void (*)(void*, void*), void*);
void forall_cpu(struct machine*, void (*)(void*, void*), void*);
void forall_memrange(struct machine*, void (*)(void*, void*), void*);
void forall_device(struct machine*, void (*)(void*, void*), void*);
void forall_cpu_in_domain(struct domain*, void (*)(void*, void*), void*);
void forall_mr_in_domain(struct domain*, void (*)(void*, void*), void*);
int get_nb_domain(struct machine*);
int get_nb_cpu(struct machine*);
int get_nb_device(struct machine*);
int get_nb_cpu_in_domain(struct domain*);
void dtb_kvmmake(void*, void*);


#endif // __TOPOLOGY__