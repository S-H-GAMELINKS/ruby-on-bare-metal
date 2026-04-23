# kernel/entry64.S - 64-bit kernel entry point

.section .text.entry, "ax"
.code64
.global _start64
_start64:
    # UEFI path passes EFI_SYSTEM_TABLE* in %rdi (SYSV arg0).
    # Multiboot path leaves %rdi unspecified — storing it is harmless
    # because Multiboot builds never read uefi_system_table.
    mov %rdi, uefi_system_table(%rip)

    # Clear 64-bit BSS
    mov $__bss64_start, %rdi
    mov $__bss64_end, %rcx
    sub %rdi, %rcx
    shr $3, %rcx
    xor %rax, %rax
    rep stosq

    # Set up 64-bit stack
    mov $stack64_top, %rsp

    call kernel_main

1:  cli
    hlt
    jmp 1b

.section .data
.balign 8
.global uefi_system_table
uefi_system_table:
    .quad 0

.section .bss
.align 16
.global stack64_bottom
stack64_bottom:
    .skip 8388608
stack64_top:
