# boot/boot.S - Multiboot entry (32-bit) + Long Mode transition

.set MULTIBOOT_MAGIC,    0x1BADB002
.set MULTIBOOT_FLAGS,    0x00000003
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

.section .multiboot, "a"
.align 4
    .long MULTIBOOT_MAGIC
    .long MULTIBOOT_FLAGS
    .long MULTIBOOT_CHECKSUM

# 32-bit boot BSS
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.align 4096
pml4:    .skip 4096
pdpt:    .skip 4096
pd:      .skip 4096

# 64-bit GDT (in rodata)
.section .rodata
.align 16
gdt64:
    .quad 0x0000000000000000    # null
    .quad 0x00AF9A000000FFFF    # 64-bit code
    .quad 0x00CF92000000FFFF    # data
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

# 32-bit boot code
.section .text.boot, "ax"
.code32
.global _start
_start:
    cli
    mov $stack_top, %esp

    # Clear 32-bit BSS
    mov $__bss_start, %edi
    mov $__bss_end, %ecx
    sub %edi, %ecx
    shr $2, %ecx
    xor %eax, %eax
    rep stosl

    # Identity-map first 256MB using 2MB pages (128 entries)
    mov $pdpt, %eax
    or $0x03, %eax
    mov %eax, (pml4)

    mov $pd, %eax
    or $0x03, %eax
    mov %eax, (pdpt)

    # Fill 256 PD entries: i*0x200000 | 0x83 (present+writable+PS) = 512MB
    mov $pd, %edi
    mov $0x83, %eax          # first entry: 0x000083
    xor %edx, %edx
    mov $256, %ecx
1:  mov %eax, (%edi)
    mov %edx, 4(%edi)
    add $0x200000, %eax      # next 2MB page
    adc $0, %edx
    add $8, %edi
    dec %ecx
    jnz 1b

    # Enable PAE + SSE (OSFXSR + OSXMMEXCPT)
    mov %cr4, %eax
    or $(1 << 5 | 1 << 9 | 1 << 10), %eax
    mov %eax, %cr4

    # Clear CR0.EM (bit 2) and set CR0.MP (bit 1) for FPU/SSE
    mov %cr0, %eax
    and $~(1 << 2), %eax
    or $(1 << 1), %eax
    mov %eax, %cr0

    # Load PML4 into CR3
    mov $pml4, %eax
    mov %eax, %cr3

    # Enable Long Mode (IA32_EFER.LME)
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    # Enable paging (PG + PE)
    mov %cr0, %eax
    or $0x80000001, %eax
    mov %eax, %cr0

    # Load 64-bit GDT and far jump
    lgdt (gdt64_ptr)
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Jump to 64-bit kernel entry at 2MB
    mov $0x200000, %rax
    jmp *%rax

1:  cli
    hlt
    jmp 1b

# Embedded 64-bit kernel binary (placed at 2MB by linker)
.section .kernel64, "a"
.incbin "build/kernel64.bin"
