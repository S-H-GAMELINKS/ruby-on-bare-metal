#include "kernel.h"
#include "../boot/uefi/efi.h"
#include <stddef.h>
#include <stdint.h>
#include "generated_misaki_font.c"

extern EFI_SYSTEM_TABLE *uefi_system_table;

static EFI_INPUT_KEY pending_key;
static int pending_key_valid = 0;
static char pending_bytes[8];
static int pending_byte_len = 0;
static int pending_byte_pos = 0;
static uint32_t utf8_codepoint = 0;
static int utf8_remaining = 0;
static int current_font_mode = 0;

static EFI_SYSTEM_TABLE *st(void) { return uefi_system_table; }

static UINTN font_span(void) {
    if (current_font_mode >= 2) return 3;
    if (current_font_mode == 1) return 2;
    return 1;
}

static void queue_input_bytes(const char *s) {
    pending_byte_pos = 0;
    pending_byte_len = 0;
    while (s[pending_byte_len] && pending_byte_len < (int)sizeof(pending_bytes)) {
        pending_bytes[pending_byte_len] = s[pending_byte_len];
        pending_byte_len++;
    }
}

/* ---- graphics console state ---- */

#define CELL_W 16
#define CELL_H 32
#define ASCII_FONT_W 5
#define ASCII_FONT_H 7
#define ASCII_FONT_SCALE 2
#define ASCII_FONT_PAD_X 3
#define ASCII_FONT_PAD_Y 9
#define MISAKI_FONT_SCALE 2
#define MISAKI_FONT_PAD_X 0
#define MISAKI_FONT_PAD_Y 8
#define FONT_H ASCII_FONT_H

static EFI_GUID gop_guid = {
    0x9042a9de, 0x23dc, 0x4a38,
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
};

static int gfx_enabled = 0;
static int gfx_rotate_cw = 0;
static uint32_t *fb = 0;
static UINTN fb_width = 0;
static UINTN fb_height = 0;
static UINTN fb_stride = 0;
static EFI_GRAPHICS_PIXEL_FORMAT fb_format = PixelBlueGreenRedReserved8BitPerColor;
static EFI_PIXEL_BITMASK fb_mask;
static UINTN logical_width = 0;
static UINTN logical_height = 0;
static UINTN cursor_col = 0;
static UINTN cursor_row = 0;
static UINTN max_cols = 80;
static UINTN max_rows = 25;
static UINTN current_attr = 0x07;

static uint32_t compose_color(uint8_t r, uint8_t g, uint8_t b) {
    switch (fb_format) {
    case PixelRedGreenBlueReserved8BitPerColor:
        return ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    case PixelBlueGreenRedReserved8BitPerColor:
        return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
    case PixelBitMask: {
        uint32_t c = 0;
        if (fb_mask.RedMask)   c |= (((uint32_t)r << 8) & fb_mask.RedMask);
        if (fb_mask.GreenMask) c |= (((uint32_t)g << 8) & fb_mask.GreenMask);
        if (fb_mask.BlueMask)  c |= (((uint32_t)b << 8) & fb_mask.BlueMask);
        return c;
    }
    default:
        return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
    }
}

static const uint8_t *ascii_glyph_rows(char c) {
    static const uint8_t blank[FONT_H] = { 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t question[FONT_H] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
    static const uint8_t excl[FONT_H] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
    static const uint8_t dot[FONT_H] = { 0, 0, 0, 0, 0, 0x0c, 0x0c };
    static const uint8_t comma[FONT_H] = { 0, 0, 0, 0, 0x0c, 0x0c, 0x08 };
    static const uint8_t colon[FONT_H] = { 0, 0x0c, 0x0c, 0, 0x0c, 0x0c, 0 };
    static const uint8_t semicolon[FONT_H] = { 0, 0x0c, 0x0c, 0, 0x0c, 0x0c, 0x08 };
    static const uint8_t minus[FONT_H] = { 0, 0, 0, 0x1f, 0, 0, 0 };
    static const uint8_t plus[FONT_H] = { 0, 0x04, 0x04, 0x1f, 0x04, 0x04, 0 };
    static const uint8_t slash[FONT_H] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0, 0 };
    static const uint8_t lparen[FONT_H] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
    static const uint8_t rparen[FONT_H] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
    static const uint8_t equal[FONT_H] = { 0, 0x1f, 0, 0x1f, 0, 0, 0 };
    static const uint8_t hash[FONT_H] = { 0x0a, 0x1f, 0x0a, 0x0a, 0x1f, 0x0a, 0 };
    static const uint8_t percent[FONT_H] = { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 };
    static const uint8_t tilde[FONT_H] = { 0, 0x0a, 0x15, 0, 0, 0, 0 };
    static const uint8_t lbracket[FONT_H] = { 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e };
    static const uint8_t rbracket[FONT_H] = { 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e };
    static const uint8_t underscore[FONT_H] = { 0, 0, 0, 0, 0, 0, 0x1f };

    static const uint8_t zero[FONT_H]  = { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e };
    static const uint8_t one[FONT_H]   = { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e };
    static const uint8_t two[FONT_H]   = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f };
    static const uint8_t three[FONT_H] = { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e };
    static const uint8_t four[FONT_H]  = { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 };
    static const uint8_t five[FONT_H]  = { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e };
    static const uint8_t six[FONT_H]   = { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e };
    static const uint8_t seven[FONT_H] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
    static const uint8_t eight[FONT_H] = { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e };
    static const uint8_t nine[FONT_H]  = { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e };

    static const uint8_t A[FONT_H] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
    static const uint8_t B[FONT_H] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e };
    static const uint8_t C[FONT_H] = { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e };
    static const uint8_t D[FONT_H] = { 0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c };
    static const uint8_t E[FONT_H] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f };
    static const uint8_t F[FONT_H] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 };
    static const uint8_t G[FONT_H] = { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f };
    static const uint8_t H[FONT_H] = { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
    static const uint8_t I[FONT_H] = { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e };
    static const uint8_t J[FONT_H] = { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c };
    static const uint8_t K[FONT_H] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
    static const uint8_t L[FONT_H] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f };
    static const uint8_t M[FONT_H] = { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 };
    static const uint8_t N[FONT_H] = { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 };
    static const uint8_t O[FONT_H] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
    static const uint8_t P[FONT_H] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 };
    static const uint8_t Q[FONT_H] = { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d };
    static const uint8_t R[FONT_H] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 };
    static const uint8_t S[FONT_H] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e };
    static const uint8_t T[FONT_H] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
    static const uint8_t U[FONT_H] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
    static const uint8_t V[FONT_H] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 };
    static const uint8_t W[FONT_H] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a };
    static const uint8_t X[FONT_H] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 };
    static const uint8_t Y[FONT_H] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 };
    static const uint8_t Z[FONT_H] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f };

    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));

    switch (c) {
    case ' ': return blank;
    case '?': return question;
    case '!': return excl;
    case '.': return dot;
    case ',': return comma;
    case ':': return colon;
    case ';': return semicolon;
    case '-': return minus;
    case '+': return plus;
    case '/': return slash;
    case '(': return lparen;
    case ')': return rparen;
    case '=': return equal;
    case '#': return hash;
    case '%': return percent;
    case '~': return tilde;
    case '[': return lbracket;
    case ']': return rbracket;
    case '_': return underscore;
    case '0': return zero;
    case '1': return one;
    case '2': return two;
    case '3': return three;
    case '4': return four;
    case '5': return five;
    case '6': return six;
    case '7': return seven;
    case '8': return eight;
    case '9': return nine;
    case 'A': return A;
    case 'B': return B;
    case 'C': return C;
    case 'D': return D;
    case 'E': return E;
    case 'F': return F;
    case 'G': return G;
    case 'H': return H;
    case 'I': return I;
    case 'J': return J;
    case 'K': return K;
    case 'L': return L;
    case 'M': return M;
    case 'N': return N;
    case 'O': return O;
    case 'P': return P;
    case 'Q': return Q;
    case 'R': return R;
    case 'S': return S;
    case 'T': return T;
    case 'U': return U;
    case 'V': return V;
    case 'W': return W;
    case 'X': return X;
    case 'Y': return Y;
    case 'Z': return Z;
    default: return question;
    }
}

static const uint8_t *misaki_glyph_rows(uint32_t codepoint) {
    for (unsigned long i = 0; i < generated_misaki_glyph_count; i++) {
        if (generated_misaki_glyphs[i].codepoint == codepoint) {
            return generated_misaki_glyphs[i].rows;
        }
    }
    return NULL;
}

static void put_pixel(UINTN x, UINTN y, uint32_t color) {
    UINTN px;
    UINTN py;

    if (!gfx_enabled || !fb) return;
    if (x >= logical_width || y >= logical_height) return;

    if (gfx_rotate_cw) {
        px = fb_width - 1 - y;
        py = x;
    } else {
        px = x;
        py = y;
    }

    if (px >= fb_width || py >= fb_height) return;
    fb[py * fb_stride + px] = color;
}

static uint32_t attr_color(int fg) {
    static const uint8_t colors[16][3] = {
        { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0xaa }, { 0x00, 0xaa, 0x00 }, { 0x00, 0xaa, 0xaa },
        { 0xaa, 0x00, 0x00 }, { 0xaa, 0x00, 0xaa }, { 0xaa, 0x55, 0x00 }, { 0xaa, 0xaa, 0xaa },
        { 0x55, 0x55, 0x55 }, { 0x55, 0x55, 0xff }, { 0x55, 0xff, 0x55 }, { 0x55, 0xff, 0xff },
        { 0xff, 0x55, 0x55 }, { 0xff, 0x55, 0xff }, { 0xff, 0xff, 0x55 }, { 0xff, 0xff, 0xff }
    };
    int idx = fg ? (current_attr & 0x0f) : ((current_attr >> 4) & 0x0f);
    return compose_color(colors[idx][0], colors[idx][1], colors[idx][2]);
}

static void fill_rect(UINTN x, UINTN y, UINTN w, UINTN h, uint32_t color) {
    for (UINTN yy = 0; yy < h; yy++) {
        for (UINTN xx = 0; xx < w; xx++) {
            put_pixel(x + xx, y + yy, color);
        }
    }
}

static void draw_ascii_glyph(UINTN col, UINTN row, char c, int mode) {
    UINTN x0 = col * CELL_W;
    UINTN y0 = row * CELL_H;
    const uint8_t *rows = ascii_glyph_rows(c);
    uint32_t bg = attr_color(0);
    uint32_t fg = attr_color(1);
    UINTN span = font_span();
    int scale = ASCII_FONT_SCALE;
    int pad_x = ASCII_FONT_PAD_X;
    int pad_y = ASCII_FONT_PAD_Y;
    UINTN box_h = CELL_H;

    if (mode >= 2) {
        scale = 6;
        pad_x = 9;
        pad_y = 3;
        box_h = CELL_H * 2;
    } else if (mode == 1) {
        scale = 4;
        pad_x = 6;
        pad_y = 2;
    }

    fill_rect(x0, y0, CELL_W * span, box_h, bg);

    for (int gy = 0; gy < ASCII_FONT_H; gy++) {
        uint8_t bits = rows[gy];
        for (int gx = 0; gx < ASCII_FONT_W; gx++) {
            if (!(bits & (1u << (ASCII_FONT_W - 1 - gx)))) continue;
            fill_rect(x0 + pad_x + gx * scale,
                      y0 + pad_y + gy * scale,
                      scale, scale, fg);
        }
    }
}

static void draw_misaki_glyph(UINTN col, UINTN row, const uint8_t *rows, int mode) {
    UINTN x0 = col * CELL_W;
    UINTN y0 = row * CELL_H;
    uint32_t bg = attr_color(0);
    uint32_t fg = attr_color(1);
    UINTN span = font_span();
    int scale = MISAKI_FONT_SCALE;
    int pad_x = MISAKI_FONT_PAD_X;
    int pad_y = MISAKI_FONT_PAD_Y;
    UINTN box_h = CELL_H;

    if (mode >= 2) {
        scale = 6;
        pad_x = 0;
        pad_y = 0;
        box_h = CELL_H * 2;
    } else if (mode == 1) {
        scale = 4;
        pad_x = 0;
        pad_y = 0;
    }

    fill_rect(x0, y0, CELL_W * span, box_h, bg);

    for (int gy = 0; gy < 8; gy++) {
        uint8_t bits = rows[gy];
        for (int gx = 0; gx < 8; gx++) {
            if (!(bits & (1u << (7 - gx)))) continue;
            fill_rect(x0 + pad_x + gx * scale,
                      y0 + pad_y + gy * scale,
                      scale, scale, fg);
        }
    }
}

static void draw_codepoint_glyph(UINTN col, UINTN row, uint32_t codepoint) {
    const uint8_t *rows;
    int mode = current_font_mode;

    if (codepoint < 0x80) {
        draw_ascii_glyph(col, row, (char)codepoint, mode);
        return;
    }

    rows = misaki_glyph_rows(codepoint);
    if (rows) {
        draw_misaki_glyph(col, row, rows, mode);
        return;
    }

    draw_ascii_glyph(col, row, '?', mode);
}

static void gfx_scroll(void) {
    uint32_t bg = attr_color(0);
    UINTN src_y = CELL_H;
    UINTN dst_y = 0;
    UINTN copy_h = logical_height > CELL_H ? logical_height - CELL_H : 0;

    for (UINTN y = 0; y < copy_h; y++) {
        for (UINTN x = 0; x < logical_width; x++) {
            UINTN spx, spy, dpx, dpy;
            if (gfx_rotate_cw) {
                spx = fb_width - 1 - (src_y + y);
                spy = x;
                dpx = fb_width - 1 - (dst_y + y);
                dpy = x;
            } else {
                spx = x;
                spy = src_y + y;
                dpx = x;
                dpy = dst_y + y;
            }
            fb[dpy * fb_stride + dpx] = fb[spy * fb_stride + spx];
        }
    }

    fill_rect(0, copy_h, logical_width, logical_height - copy_h, bg);
    cursor_row = max_rows ? max_rows - 1 : 0;
}

static void console_clear(void) {
    if (gfx_enabled) {
        fill_rect(0, 0, logical_width, logical_height, attr_color(0));
        cursor_col = 0;
        cursor_row = 0;
        current_font_mode = 0;
        return;
    }
    if (st() && st()->ConOut) st()->ConOut->ClearScreen(st()->ConOut);
}

static void console_set_cursor(UINTN col, UINTN row) {
    if (gfx_enabled) {
        cursor_col = col < max_cols ? col : max_cols - 1;
        cursor_row = row < max_rows ? row : max_rows - 1;
        return;
    }
    if (st() && st()->ConOut) st()->ConOut->SetCursorPosition(st()->ConOut, col, row);
}

static void console_set_attr(UINTN attr) {
    current_attr = attr;
    if (!gfx_enabled && st() && st()->ConOut) st()->ConOut->SetAttribute(st()->ConOut, attr);
}

static void console_put_codepoint(uint32_t codepoint) {
    UINTN span = font_span();

    if (!gfx_enabled) {
        if (!st() || !st()->ConOut) return;
        CHAR16 buf[3];
        if (codepoint == '\n') { buf[0] = '\r'; buf[1] = '\n'; buf[2] = 0; }
        else                   { buf[0] = (CHAR16)codepoint; buf[1] = 0; }
        st()->ConOut->OutputString(st()->ConOut, buf);
        return;
    }

    if (codepoint == '\r') {
        cursor_col = 0;
        return;
    }
    if (codepoint == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= max_rows) gfx_scroll();
        return;
    }
    if (codepoint == '\t') {
        UINTN next = (cursor_col + 8) & ~((UINTN)7);
        while (cursor_col < next) console_put_codepoint(' ');
        return;
    }

    if (cursor_col + span > max_cols) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= max_rows) gfx_scroll();
    }

    draw_codepoint_glyph(cursor_col, cursor_row, codepoint);
    cursor_col += span;
    if (cursor_col >= max_cols) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= max_rows) gfx_scroll();
    }
}

static void raw_putc(unsigned char c) {
    if (c < 0x80) {
        utf8_codepoint = 0;
        utf8_remaining = 0;
        console_put_codepoint((uint32_t)c);
        return;
    }

    if (utf8_remaining == 0) {
        if ((c & 0xE0) == 0xC0) {
            utf8_codepoint = (uint32_t)(c & 0x1F);
            utf8_remaining = 1;
            return;
        }
        if ((c & 0xF0) == 0xE0) {
            utf8_codepoint = (uint32_t)(c & 0x0F);
            utf8_remaining = 2;
            return;
        }
        if ((c & 0xF8) == 0xF0) {
            utf8_codepoint = (uint32_t)(c & 0x07);
            utf8_remaining = 3;
            return;
        }
        console_put_codepoint('?');
        return;
    }

    if ((c & 0xC0) != 0x80) {
        utf8_codepoint = 0;
        utf8_remaining = 0;
        console_put_codepoint('?');
        raw_putc(c);
        return;
    }

    utf8_codepoint = (utf8_codepoint << 6) | (uint32_t)(c & 0x3F);
    utf8_remaining--;
    if (utf8_remaining == 0) {
        console_put_codepoint(utf8_codepoint);
    }
}

static void try_init_gfx(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;

    if (!st() || !st()->BootServices || !st()->BootServices->LocateProtocol) return;
    if (st()->BootServices->LocateProtocol(&gop_guid, 0, (VOID **)&gop) != EFI_SUCCESS) return;
    if (!gop || !gop->Mode || !gop->Mode->Info) return;

    mode = gop->Mode;
    if (mode->Info->PixelFormat == PixelBltOnly) return;

    fb = (uint32_t *)(UINTN)mode->FrameBufferBase;
    fb_width = mode->Info->HorizontalResolution;
    fb_height = mode->Info->VerticalResolution;
    fb_stride = mode->Info->PixelsPerScanLine ? mode->Info->PixelsPerScanLine : fb_width;
    fb_format = mode->Info->PixelFormat;
    fb_mask = mode->Info->PixelInformation;

    if (!fb || fb_width == 0 || fb_height == 0) return;

    gfx_rotate_cw = (fb_width < fb_height);
    logical_width = gfx_rotate_cw ? fb_height : fb_width;
    logical_height = gfx_rotate_cw ? fb_width : fb_height;
    max_cols = logical_width / CELL_W;
    max_rows = logical_height / CELL_H;
    if (max_cols == 0 || max_rows == 0) return;

    gfx_enabled = 1;
}

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

    if (final == 'H' || final == 'f') {
        int row = (csi_n >= 1 && csi_params[0] > 0) ? csi_params[0] : 1;
        int col = (csi_n >= 2 && csi_params[1] > 0) ? csi_params[1] : 1;
        console_set_cursor((UINTN)(col - 1), (UINTN)(row - 1));
    } else if (final == 'J') {
        int n = (csi_n >= 1 && csi_params[0] >= 0) ? csi_params[0] : 0;
        if (n == 2) console_clear();
    } else if (final == 'm') {
        UINTN attr = current_attr;
        for (int i = 0; i < csi_n; i++) {
            int p = csi_params[i] < 0 ? 0 : csi_params[i];
            if (p == 0)                    attr = 0x07;
            else if (p >= 30 && p <= 37)   attr = (attr & 0xF0) | (UINTN)(p - 30);
            else if (p >= 90 && p <= 97)   attr = (attr & 0xF0) | (UINTN)(p - 90 + 8);
            else if (p >= 40 && p <= 47)   attr = (attr & 0x0F) | ((UINTN)(p - 40) << 4);
            else if (p >= 100 && p <= 107) attr = (attr & 0x0F) | ((UINTN)(p - 100 + 8) << 4);
        }
        console_set_attr(attr);
    } else if (final == 'z') {
        int mode = (csi_n >= 1 && csi_params[0] > 0) ? csi_params[0] : 0;
        if (mode < 0) mode = 0;
        if (mode > 2) mode = 2;
        current_font_mode = mode;
    }
}

static void parse_byte(unsigned char c) {
    switch (ansi_state) {
    case ST_NORMAL:
        if (c == 0x1B) { ansi_state = ST_ESC; }
        else           { raw_putc((char)c); }
        return;
    case ST_ESC:
        if (c == '[') { ansi_reset(); ansi_state = ST_CSI; }
        else          { ansi_state = ST_NORMAL; }
        return;
    case ST_CSI:
        if (c >= '0' && c <= '9') {
            csi_cur = csi_cur * 10 + (c - '0');
            csi_seen_digit = 1;
        } else if (c == ';') {
            ansi_push_param();
        } else if (c == '?' || c == '>' || c == '=' || c == ' ') {
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
    try_init_gfx();
    ansi_reset();
    current_attr = 0x07;
    current_font_mode = 0;
    console_clear();
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
    if (pending_byte_pos < pending_byte_len) return 1;
    if (pending_key_valid) return 1;
    if (st()->ConIn->ReadKeyStroke(st()->ConIn, &pending_key) == EFI_SUCCESS) {
        pending_key_valid = 1;
        return 1;
    }
    return 0;
}

int uefi_console_getc(void) {
    if (!st() || !st()->ConIn) return -1;
    if (pending_byte_pos < pending_byte_len) {
        return (unsigned char)pending_bytes[pending_byte_pos++];
    }
    if (!pending_key_valid) {
        while (st()->ConIn->ReadKeyStroke(st()->ConIn, &pending_key) == EFI_NOT_READY) {
            __asm__ volatile("pause");
        }
    }
    pending_key_valid = 0;
    if (pending_key.UnicodeChar == '\r') return '\n';
    if (pending_key.UnicodeChar) return (int)(unsigned char)pending_key.UnicodeChar;
    switch (pending_key.ScanCode) {
    case 1: queue_input_bytes("\x1b[A"); break;
    case 2: queue_input_bytes("\x1b[B"); break;
    case 3: queue_input_bytes("\x1b[C"); break;
    case 4: queue_input_bytes("\x1b[D"); break;
    case 9: queue_input_bytes("\x1b[5~"); break;
    case 10: queue_input_bytes("\x1b[6~"); break;
    case 17: return '\x1b';
    default: break;
    }
    if (pending_byte_pos < pending_byte_len) {
        return (unsigned char)pending_bytes[pending_byte_pos++];
    }
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

unsigned long uefi_console_cols(void) {
    return (unsigned long)max_cols;
}

unsigned long uefi_console_rows(void) {
    return (unsigned long)max_rows;
}

unsigned long uefi_console_pixel_width(void) {
    return (unsigned long)logical_width;
}

unsigned long uefi_console_pixel_height(void) {
    return (unsigned long)logical_height;
}
