#include "kernel.h"
#include <stddef.h>

/* Embedded file table structure */
struct embedded_file {
    const char *path;
    const char *data;
    size_t size;
};

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static int starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

/* Generated data */
#include "generated_scripts.c"
#include "generated_mui.c"

/* ---- Public API ---- */

int ruby_on_bare_metal_file_exists(const char *path) {
    if (!path) return 0;
    for (int i = 0; generated_files[i].path; i++) {
        if (streq(path, generated_files[i].path)) return 1;
    }
    for (int i = 0; mui_files[i].path; i++) {
        if (streq(path, mui_files[i].path)) return 1;
    }
    return 0;
}

const char *ruby_on_bare_metal_embedded_file_data(const char *path, size_t *size) {
    if (!path) return NULL;
    for (int i = 0; generated_files[i].path; i++) {
        if (streq(path, generated_files[i].path)) {
            if (size) *size = generated_files[i].size;
            return generated_files[i].data;
        }
    }
    for (int i = 0; mui_files[i].path; i++) {
        if (streq(path, mui_files[i].path)) {
            if (size) *size = mui_files[i].size;
            return mui_files[i].data;
        }
    }
    return NULL;
}

/* List files for VFS directory listing */
int ruby_on_bare_metal_list_files(const char *prefix,
                      void (*callback)(const char *path, size_t size, void *ctx),
                      void *ctx) {
    int count = 0;
    for (int i = 0; generated_files[i].path; i++) {
        if (starts_with(generated_files[i].path, prefix)) {
            callback(generated_files[i].path, generated_files[i].size, ctx);
            count++;
        }
    }
    for (int i = 0; mui_files[i].path; i++) {
        if (starts_with(mui_files[i].path, prefix)) {
            callback(mui_files[i].path, mui_files[i].size, ctx);
            count++;
        }
    }
    return count;
}
