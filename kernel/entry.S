.data
	# qemu -kernel loads the kernel at 0x80000000
        # and causes each CPU to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
.global _boot

_entry:
        # Save _entry physical address
        auipc t0, 0
        la t1, p_entry
        sw t0, 0(t1)
        # Ensure to start on hart 0
        beqz a0, _preboot
        la sp, stack1
        # start hart 0
        li a7, 0x48534D
        li a6, 0
        li a0, 0
        addi a2, a1, 0
        la a1, _preboot
        ecall
        # stop current hart
        li a7, 0x48534D
        li a6, 1
        ecall

_preboot:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        la sp, stack0
        call preboot

_boot:
        # Save _entry physical address (part 1)
        auipc t0, 0
        # Use a temporary page table
        csrw satp, a1
        sfence.vma zero, zero
        # Save _entry physical address (part 2)
        la t1, p_entry
        sw t0, 0(t1)
        # set up a stack for C.
        la sp, stack0
        call boot

spin:
        j spin
