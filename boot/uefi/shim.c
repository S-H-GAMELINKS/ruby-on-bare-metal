#include "efi.h"

#define KERNEL_LOAD_ADDR  0x200000UL

extern unsigned char kernel64_uefi_bin_start[];
extern unsigned char kernel64_uefi_bin_end[];

static void utf16_from_ascii(CHAR16 *dst, const char *src, UINTN max) {
    UINTN i = 0;
    while (src[i] && i + 1 < max) { dst[i] = (CHAR16)src[i]; i++; }
    dst[i] = 0;
}

static void println(EFI_SYSTEM_TABLE *st, const char *msg) {
    CHAR16 buf[128];
    utf16_from_ascii(buf, msg, 126);
    UINTN i = 0; while (buf[i]) i++;
    buf[i] = '\r'; buf[i+1] = '\n'; buf[i+2] = 0;
    st->ConOut->OutputString(st->ConOut, buf);
}

typedef void (*kernel_entry_t)(EFI_SYSTEM_TABLE *st) __attribute__((sysv_abi));

EFIAPI EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st) {
    (void)image_handle;

    st->ConOut->ClearScreen(st->ConOut);
    println(st, "Ruby on Bare Metal - UEFI shim");
    println(st, "allocating pages at 0x200000...");

    UINTN kernel_size = (UINTN)(kernel64_uefi_bin_end - kernel64_uefi_bin_start);
    UINTN pages = (kernel_size + 0xFFF) / 0x1000;
    UINT64 addr = KERNEL_LOAD_ADDR;

    EFI_STATUS s = st->BootServices->AllocatePages(
        AllocateAddress, EfiLoaderData, pages, &addr);
    if (s != EFI_SUCCESS) {
        println(st, "AllocatePages failed, retrying AnyPages...");
        s = st->BootServices->AllocatePages(
            AllocateAnyPages, EfiLoaderData, pages, &addr);
        if (s != EFI_SUCCESS) {
            println(st, "fatal: cannot allocate kernel memory");
            return s;
        }
    }

    unsigned char *dst = (unsigned char *)(UINTN)addr;
    for (UINTN i = 0; i < kernel_size; i++) dst[i] = kernel64_uefi_bin_start[i];

    println(st, "jumping to kernel...");

    kernel_entry_t entry = (kernel_entry_t)(UINTN)addr;
    entry(st);

    for (;;) __asm__ volatile("hlt");
}
