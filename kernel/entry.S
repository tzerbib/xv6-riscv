	# qemu -kernel loads the kernel at 0x80000000
        # and causes each CPU to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
.global _boot
.global p_entry

_entry:
        # Save _entry physical address
        auipc t0, 0
        la t1, p_entry
        sw t0, 0(t1)
        # Ensure to start on hart 0
        beqz a0, _boot
        # sp = stack0 + 4096
        la sp, stack0 + 4096
        # start hart 0
        li a7, 0x48534D
        li a6, 0
        li a0, 0
        addi a2, a1, 0
        la a1, _boot
        ecall
        # stop current hart
        li a7, 0x48534D
        li a6, 1
        ecall

_boot:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        la sp, stack0
        call main

spin:
        j spin


# Physical address in RAM corresponding to the kernel text start
p_entry:
        .dword 0
