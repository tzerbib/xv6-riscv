.data
	# qemu -kernel loads the kernel at 0x80200000
        # kernel.ld causes the following code to
        # be placed at 0x80200000.
.section .text
.global _entry
.global _masters_boot
.global _slaves_boot
.global _masters_wakeup


# The hart winning OpenSBI hart lottery will jump here
_entry:
        # Save _entry physical address
        auipc t0, 0
        la t1, p_entry
        sw t0, 0(t1)
        # Ensure to start on hart 0
        beqz a0, _boot
        la sp, stack1
        # start hart 0 (sbi_start_hart)
        li a7, 0x48534D
        li a6, 0
        li a0, 0
        addi a2, a1, 0
        la a1, _boot
        ecall
        # stop current hart (sbi_stop_hart)
        li a7, 0x48534D
        li a6, 1
        ecall
        j spin


# Entry point for hart 0 (after filtering by _entry)
_boot:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        la sp, stack0
        call machine_master_start
        j spin


# Domain masters first wakeup here while in hart0's kernel image
_masters_wakeup:
        addi sp, a1, 0
        call domain_master_wakeup_start


# Domain masters jump here when the File System has been initialized.
# This is because the FS initialization needs to be done in U-mode.
# Moreover, kexec needs to load kernel images from the FS.
# Finally, only harts pluged on the same domain than the disk can access
# the right PLIC.
_masters_boot:
        # Get _entry physical address
        auipc t0, 0
        # Use a temporary page table
        csrw satp, a1
        sfence.vma zero, zero
        # Save _entry physical address (now that paging has been enabled)
        la t1, p_entry
        sw t0, 0(t1)
        # set up a stack for C.
        la sp, stack0
        # Prepare parameters
        addi a1, a2, 0
        # Absolute jump to va domain_master_hart = 0x80200xxx (pa: 82000xxx)
        la t0, domain_master_start
        jalr t0, 0
        j spin


_slaves_boot:
        # set up a stack for C.
        addi sp, a1, 0
        # Use a temporary page table
        ld t0, -8(sp)
        csrw satp, t0
        sfence.vma zero, zero
        call slave_start


spin:
        j spin
