    .section .rdata, "dr"
    .globl kernel64_uefi_bin_start
    .globl kernel64_uefi_bin_end
    .balign 16
kernel64_uefi_bin_start:
    .incbin "build/kernel64_uefi.bin"
kernel64_uefi_bin_end:
