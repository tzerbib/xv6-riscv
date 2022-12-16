// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT (one per socket, starting at this address)
// 0C000000 -- PLIC (one per socket, starting at this address)
// 10000000 -- uart0
// 10001000 -- virtio disk
// 80000000 -- boot ROM jumps here in machine mode
//             -kernel loads the kernel here
// unused RAM after 80000000.

// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

//[numa]XXX For now, we don't know how to discover the number of harts and
// sockets, and how they are mapped to each other.
// Macros below assume assigning harts as qemu does it when the number of harts
// per socket is a power of 2 (and the same for all sockets).
#ifndef NB_SOCKETS
#define NB_SOCKETS 1
#endif
//[numa] Total number of harts (not the number of harts per socket)
#ifndef NB_HARTS
#define NB_HARTS 2
#endif

#ifndef MAX_NB_HARTS_PER_SOCKET
#define MAX_NB_HARTS_PER_SOCKET 8
#endif

#define NB_HARTS_PER_SOCKET (NB_HARTS/NB_SOCKETS)
#define HART_SOCKETID(hartid) ((hartid)/NB_HARTS_PER_SOCKET)
#define HARTID_IN_SOCKET(hartid) ((hartid)%NB_HARTS_PER_SOCKET)


// qemu puts platform-level interrupt controller (PLIC) here.
//[numa] There is one PLIC per NUMA socket.
#define PLIC                        0x0c000000L     // Base address of all PLICs
//[numa] Offsets are within one socket's PLIC.
#define PLIC_PRIORITY_OFF           0x0
#define PLIC_PENDING_OFF            0x1000
#define PLIC_ENABLE_OFF             0x2000
#define PLIC_PRIOTHRESH_OFF         0x200000
#define PLIC_CLAIMCOMPL_OFF         0x200004
#define PLIC_PRIORITY_SZ            0x4
#define PLIC_PENDING_SZ             0x4
//[numa] There are 2 contexts per hart, which actually are the privilege levels
// that can issue IRQs at the platform level: machine and supervisor.
// For each hart, context 0 is machine mode, 1 (i.e., at the corresponding
// additional offset) is supervisor mode.
#define PLIC_ENABLE_CONTEXTSZ       0x80
#define PLIC_PRIOTHRESH_CONTEXTSZ   0x1000
#define PLIC_CLAIMCOMPL_CONTEXTSZ   PLIC_PRIOTHRESH_CONTEXTSZ
//[numa] RISCV PLIC spec says the size is 0x4000000 (with reserved bits starting
// at 0x3fff008). In QEMU, the size is the fixed size of the interrupt
// priorities and enable bits registers (represented by PLIC_PRIOTHRESH_OFF
// because the area of priority thresholds comes just afterwards), plus the
// priority threshold and claim/complete registers for each mode (2: machine and
// supervisor) for the maximum possible number of CPUs per socket (8).
// This is the size of one socket's PLIC.
#define PLIC_SZ                     (PLIC_PRIOTHRESH_OFF + 2*MAX_NB_HARTS_PER_SOCKET*PLIC_PRIOTHRESH_CONTEXTSZ)

#define PLIC_PRIORITY(socketid, irqid) (PLIC + (socketid)*PLIC_SZ + PLIC_PRIORITY_OFF + (irqid)*PLIC_PRIORITY_SZ)
#define PLIC_SENABLE(hartid) (PLIC + HART_SOCKETID(hartid)*PLIC_SZ + PLIC_ENABLE_OFF + HARTID_IN_SOCKET(hartid)*2*PLIC_ENABLE_CONTEXTSZ + PLIC_ENABLE_CONTEXTSZ)
#define PLIC_SPRIOTHRESH(hartid) (PLIC + HART_SOCKETID(hartid)*PLIC_SZ + PLIC_PRIOTHRESH_OFF + HARTID_IN_SOCKET(hartid)*2*PLIC_PRIOTHRESH_CONTEXTSZ + PLIC_PRIOTHRESH_CONTEXTSZ)
#define PLIC_SCLAIM(hartid) (PLIC + HART_SOCKETID(hartid)*PLIC_SZ + PLIC_CLAIMCOMPL_OFF + HARTID_IN_SOCKET(hartid)*2*PLIC_CLAIMCOMPL_CONTEXTSZ + PLIC_CLAIMCOMPL_CONTEXTSZ)
#define PLIC_SCOMPLETE PLIC_SCLAIM

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address KERNBASE to PHYSTOP.
// The OpenSBI firmware is at 0x80000000 (RAM start).
// It should have been built to jump at the same KERNBASE address.
#ifndef KERNBASE
#define KERNBASE 0x80200000L
#endif
#define PHYSTOP 0x84000000

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)

// Machine master is always cpu 0
#define IS_MMASTER (!cpuid())