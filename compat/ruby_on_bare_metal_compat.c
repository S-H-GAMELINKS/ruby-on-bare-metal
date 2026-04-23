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

    ruby_script("ruby_on_bare_metal");

    /* Select entry script: UEFI build runs the autonomous dungeon demo
     * (no keyboard required); QEMU/Multiboot build keeps the interactive
     * Ruby prompt via init.rb. */
#ifdef RUBY_ON_BARE_METAL_UEFI
    const char *entry_script = "/autodemo.rb";
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
            VALUE msg = rb_funcall(err, rb_intern("message"), 0);
            const char *str = rb_string_value_cstr(&msg);
            if (str) {
                serial_puts("  ");
                serial_puts(str);
                serial_putc('\n');
            }
        }
    }
    serial_puts("ruby eval done\n");
}
