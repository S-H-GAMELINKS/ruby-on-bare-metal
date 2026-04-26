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

    # UEFI firmware does not guarantee the same floating-point state as the
    # Multiboot path. CRuby and the autodemo use libm immediately, so bring
    # x87/SSE to a known-good state here for both boot paths.
    mov %cr4, %rax
    or $(1 << 9 | 1 << 10), %rax
    mov %rax, %cr4

    mov %cr0, %rax
    and $~(1 << 2), %rax
    or $(1 << 1), %rax
    mov %rax, %cr0

    fninit
    ldmxcsr default_mxcsr(%rip)
    fldcw default_fcw(%rip)

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

.balign 4
default_mxcsr:
    .long 0x1f80

.balign 2
default_fcw:
    .word 0x037f

.section .bss
.align 16
.global stack64_bottom
stack64_bottom:
    .skip 8388608
stack64_top:
