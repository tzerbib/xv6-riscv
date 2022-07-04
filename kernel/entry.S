# qemu -bios <opensbi> -kernel <kernel> loads the kernel at 0x80200000 with
# OpenSBI at 0x80000000.
# Linker (through the Makefile) puts the kernel at the correct address of
# 0x80200000, and OpenSBI jumps here.
.section .text
.global _entry
_entry:
    # Set up a stack for C.
    # stack0 is declared in start.c, with a 4096-byte stack per CPU.
    # sp = stack0 + ((hartid+1) * 4096)
    la sp, stack0
    li a2, 1024*4
    addi a3, a0, 1
    mul a2, a2, a3
    add sp, sp, a2
    # Jump to master hart's main
    call master_main
spin:
    j spin
# Slave harts (i.e., not the boot hart that runs _entry above) are started from
# master_main at slave_entry.
.global slave_entry
slave_entry:
    # Set up a stack for C.
    # stack0 is declared in start.c, with a 4096-byte stack per CPU.
    # sp = stack0 + ((hartid+1) * 4096)
    la sp, stack0
    li a2, 1024*4
    addi a3, a0, 1
    mul a2, a2, a3
    add sp, sp, a2
    # Jump to start() in start.c.
    call slave_main
    j spin
