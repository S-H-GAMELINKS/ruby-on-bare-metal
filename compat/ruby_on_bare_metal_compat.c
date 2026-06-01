/*
 * ruby_on_bare_metal_compat.c - CRuby integration for Ruby on Bare Metal
 */

#include "../kernel/kernel.h"
#include <stddef.h>

/* CRuby API */
void ruby_init_stack(volatile void *addr);
int ruby_setup(void);
void ruby_script(const char *name);
int rb_eval_string_protect(const char *str, int *pstate);

typedef unsigned long VALUE;
VALUE rb_errinfo(void);
VALUE rb_funcall(VALUE recv, unsigned long mid, int argc, ...);
unsigned long rb_intern(const char *name);
const char *rb_string_value_cstr(volatile VALUE *ptr);

void ruby_on_bare_metal_console_write(const char *buf, size_t len) {
    serial_write(buf, len);
}

static char *append_ulong(char *p, unsigned long value) {
    char tmp[32];
    int n = 0;

    if (value == 0) {
        *p++ = '0';
        return p;
    }

    while (value && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n > 0) *p++ = tmp[--n];
    return p;
}

static void define_console_size_globals(void) {
    char code[192];
    char *p = code;

    const char prefix[] = "$rbm_console_cols=";
    const char rows[] = ";$rbm_console_rows=";
    const char xpixel[] = ";$rbm_console_xpixel=";
    const char ypixel[] = ";$rbm_console_ypixel=";
    const char suffix[] = "\n";

    for (size_t i = 0; i < sizeof(prefix) - 1; i++) *p++ = prefix[i];
    p = append_ulong(p, uefi_console_cols());
    for (size_t i = 0; i < sizeof(rows) - 1; i++) *p++ = rows[i];
    p = append_ulong(p, uefi_console_rows());
    for (size_t i = 0; i < sizeof(xpixel) - 1; i++) *p++ = xpixel[i];
    p = append_ulong(p, uefi_console_pixel_width());
    for (size_t i = 0; i < sizeof(ypixel) - 1; i++) *p++ = ypixel[i];
    p = append_ulong(p, uefi_console_pixel_height());
    for (size_t i = 0; i < sizeof(suffix) - 1; i++) *p++ = suffix[i];
    *p = 0;

    int state = 0;
    rb_eval_string_protect(code, &state);
}

void ruby_on_bare_metal_cruby_demo(void) {
    volatile int stack_anchor = 0;
    int state = 0;

    ruby_init_stack((void *)&stack_anchor);
    ruby_setup();

    /* Load builtin Ruby methods (numeric.rb, io.rb, etc.)
     * Normally called via ruby_process_options -> ruby_opt_init,
     * but we skip command-line processing. */
    extern void rb_call_builtin_inits(void);
    rb_call_builtin_inits();
    define_console_size_globals();

    ruby_script("ruby_on_bare_metal");

    /* Select entry script: UEFI build runs the autonomous dungeon demo
     * (no keyboard required); QEMU/Multiboot build keeps the interactive
     * Ruby prompt via init.rb. */
#ifdef RUBY_ON_BARE_METAL_UEFI
    const char *entry_script = "/slides.rb";
#else
    const char *entry_script = "/init.rb";
#endif
    size_t size = 0;
    const char *script = ruby_on_bare_metal_embedded_file_data(entry_script, &size);
    if (!script) {
        panic("entry script not found");
    }

    serial_puts("ruby eval start\n");
    rb_eval_string_protect(script, &state);

    if (state) {
        serial_puts("ruby exception:\n");
        VALUE err = rb_errinfo();
        if (err) {
            VALUE klass = rb_funcall(err, rb_intern("class"), 0);
            if (klass) {
                VALUE klass_name = rb_funcall(klass, rb_intern("to_s"), 0);
                const char *klass_str = rb_string_value_cstr(&klass_name);
                if (klass_str) {
                    serial_puts("  class: ");
                    serial_puts(klass_str);
                    serial_putc('\n');
                }
            }
            VALUE msg = rb_funcall(err, rb_intern("message"), 0);
            const char *str = rb_string_value_cstr(&msg);
            if (str) {
                serial_puts("  message: ");
                serial_puts(str);
                serial_putc('\n');
            }
        }
    }
    serial_puts("ruby eval done\n");
}
