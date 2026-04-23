#include "kernel.h"
#include "../boot/uefi/efi.h"

extern EFI_SYSTEM_TABLE *uefi_system_table;

static EFI_INPUT_KEY pending_key;
static int pending_key_valid = 0;

static EFI_SYSTEM_TABLE *st(void) { return uefi_system_table; }

/* ---- ANSI escape parser ---- */

enum { ST_NORMAL, ST_ESC, ST_CSI };
static int  ansi_state = ST_NORMAL;
static int  csi_params[8];
static int  csi_n;
static int  csi_cur;
static int  csi_seen_digit;

static void ansi_reset(void) {
    ansi_state = ST_NORMAL;
    csi_n = 0;
    csi_cur = 0;
    csi_seen_digit = 0;
    for (int i = 0; i < 8; i++) csi_params[i] = 0;
}

static void ansi_push_param(void) {
    if (csi_n < 8) csi_params[csi_n] = csi_seen_digit ? csi_cur : -1;
    csi_n++;
    csi_cur = 0;
    csi_seen_digit = 0;
}

static void ansi_exec(char final) {
    ansi_push_param();
    if (!st() || !st()->ConOut) return;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out = st()->ConOut;

    if (final == 'H' || final == 'f') {
        int row = (csi_n >= 1 && csi_params[0] > 0) ? csi_params[0] : 1;
        int col = (csi_n >= 2 && csi_params[1] > 0) ? csi_params[1] : 1;
        out->SetCursorPosition(out, (UINTN)(col - 1), (UINTN)(row - 1));
    } else if (final == 'J') {
        int n = (csi_n >= 1 && csi_params[0] >= 0) ? csi_params[0] : 0;
        if (n == 2) out->ClearScreen(out);
    } else if (final == 'm') {
        /* SGR: map simple ANSI colors to UEFI 4-bit attribute.
         * UEFI attr: low 4 bits = fg, bits 4-6 = bg */
        UINTN attr = out->Mode ? (UINTN)out->Mode->Attribute : 0x07;
        for (int i = 0; i < csi_n; i++) {
            int p = csi_params[i] < 0 ? 0 : csi_params[i];
            if (p == 0)                    attr = 0x07;
            else if (p >= 30 && p <= 37)   attr = (attr & 0xF0) | (UINTN)(p - 30);
            else if (p >= 90 && p <= 97)   attr = (attr & 0xF0) | (UINTN)(p - 90 + 8);
            else if (p >= 40 && p <= 47)   attr = (attr & 0x0F) | ((UINTN)(p - 40) << 4);
            else if (p >= 100 && p <= 107) attr = (attr & 0x0F) | ((UINTN)(p - 100 + 8) << 4);
        }
        out->SetAttribute(out, attr);
    }
    /* other CSI finals are silently dropped */
}

static void raw_putc(char c) {
    if (!st() || !st()->ConOut) return;
    CHAR16 buf[3];
    if (c == '\n') { buf[0] = '\r'; buf[1] = '\n'; buf[2] = 0; }
    else           { buf[0] = (CHAR16)(unsigned char)c; buf[1] = 0; }
    st()->ConOut->OutputString(st()->ConOut, buf);
}

static void parse_byte(unsigned char c) {
    switch (ansi_state) {
    case ST_NORMAL:
        if (c == 0x1B) { ansi_state = ST_ESC; }
        else           { raw_putc((char)c); }
        return;
    case ST_ESC:
        if (c == '[') { ansi_reset(); ansi_state = ST_CSI; }
        else          { ansi_state = ST_NORMAL; } /* unsupported: drop */
        return;
    case ST_CSI:
        if (c >= '0' && c <= '9') {
            csi_cur = csi_cur * 10 + (c - '0');
            csi_seen_digit = 1;
        } else if (c == ';') {
            ansi_push_param();
        } else if (c == '?' || c == '>' || c == '=' || c == ' ') {
            /* private params / intermediates: skip but keep in CSI */
        } else if (c >= 0x40 && c <= 0x7E) {
            ansi_exec((char)c);
            ansi_state = ST_NORMAL;
        } else {
            ansi_state = ST_NORMAL;
        }
        return;
    }
}

/* ---- public console API ---- */

void uefi_console_init(void) {
    if (!st() || !st()->ConOut) return;
    st()->ConOut->ClearScreen(st()->ConOut);
    ansi_reset();
}

void uefi_console_putc(char c) { parse_byte((unsigned char)c); }

void uefi_console_puts(const char *s) {
    while (*s) parse_byte((unsigned char)*s++);
}

void uefi_console_write(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) parse_byte((unsigned char)buf[i]);
}

int uefi_console_data_ready(void) {
    if (!st() || !st()->ConIn) return 0;
    if (pending_key_valid) return 1;
    if (st()->ConIn->ReadKeyStroke(st()->ConIn, &pending_key) == EFI_SUCCESS) {
        pending_key_valid = 1;
        return 1;
    }
    return 0;
}

int uefi_console_getc(void) {
    if (!st() || !st()->ConIn) return -1;
    if (!pending_key_valid) {
        while (st()->ConIn->ReadKeyStroke(st()->ConIn, &pending_key) == EFI_NOT_READY) {
            __asm__ volatile("pause");
        }
    }
    pending_key_valid = 0;
    CHAR16 c = pending_key.UnicodeChar;
    if (c == '\r') return '\n';
    if (c) return (int)(unsigned char)c;
    return 0;
}

size_t uefi_console_read(char *buf, size_t len) {
    if (len == 0) return 0;
    buf[0] = (char)uefi_console_getc();
    size_t i = 1;
    while (i < len && uefi_console_data_ready()) {
        buf[i++] = (char)uefi_console_getc();
    }
    return i;
}
