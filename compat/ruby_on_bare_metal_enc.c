/*
 * ruby_on_bare_metal_enc.c - Static encoding initialization for Ruby on Bare Metal
 * Replaces dmyenc.c which tries to dynamically load enc/encdb.so
 */

void rb_encdb_declare(const char *name);
int rb_encdb_alias(const char *alias, const char *orig);

void
Init_enc(void)
{
    rb_encdb_declare("ASCII-8BIT");
    rb_encdb_declare("US-ASCII");
    rb_encdb_declare("UTF-8");
    rb_encdb_alias("BINARY", "ASCII-8BIT");
    rb_encdb_alias("ASCII", "US-ASCII");
}

/* No dynamic extension loading on Ruby on Bare Metal */
void
Init_ext(void)
{
}

/* No extra extensions */
void
Init_extra_exts(void)
{
}
