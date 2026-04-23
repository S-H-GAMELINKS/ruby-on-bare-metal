# Ruby on Bare Metal

> A minimal bare-metal system that runs CRuby on x86_64

[日本語版はこちら / Japanese](README_ja.md)

Ruby on Bare Metal is a small system that runs CRuby directly on bare-metal x86_64 hardware. Rather than building a Linux-compatible OS, it implements the **minimum environment CRuby needs to survive**, then builds a Ruby-native userland on top.

The [mui](https://github.com/S-H-GAMELINKS/mui) Vim-like text editor, written entirely in Ruby, runs on this kernel.

### Key Numbers

| | |
|---|---|
| Kernel image size | 4.4 MB (including CRuby + musl + mui) |
| Ruby version | CRuby 4.0.2 |
| Target | x86_64 (QEMU q35) |
| Kernel C code | ~1,000 lines |
| Implemented syscalls | 83 |
| Embedded Ruby files | 97 (mui) + 2 (init/hello) |

## Prerequisites

**OS**: Linux (Ubuntu/Debian recommended, WSL2 supported)

Install the required packages:

```bash
sudo apt-get install -y \
  clang lld llvm \
  qemu-system-x86 \
  ruby \
  git make autoconf
```

## Quick Start

```bash
# 1. Clone and build all dependencies
make setup

# 2. Build the kernel
make

# 3. Run in QEMU
make run
```

Or use the convenience script:

```bash
./setup.sh   # runs 'make setup && make'
make run
```

You should see output like:

```
boot ok
timer ok
tls ok
memory ok
cruby init ok
```

followed by the Ruby shell prompt.

## Build Targets

| Target | Description |
|---|---|
| `make setup` | Clone, patch, and build all third-party dependencies |
| `make` | Build `build/kernel.elf` (default) |
| `make run` | Launch in QEMU (512MB RAM, serial on stdio) |
| `make clean` | Remove build artifacts |
| `make distclean` | Remove build artifacts and third-party sources |

### What `make setup` Does

`make setup` runs these stages in order:

1. **`setup-clone`** — Clones CRuby v4.0.2, musl libc, and mui from their upstream repositories into `third_party/`
2. **`setup-patch`** — Applies patches from `patches/` to each library (syscall redirection for musl, threading fixes for CRuby, terminal adapter for mui)
3. **`setup-musl`** — Builds musl libc as a static library, then removes malloc/pthread objects that are replaced by Ruby on Bare Metal's own implementations
4. **`setup-cruby-host`** — Builds miniruby on the host system (needed for code generation during the cross build)
5. **`setup-cruby-cross`** — Cross-compiles CRuby as `libruby-static.a` for the bare-metal target

Each stage is idempotent — re-running `make setup` skips already-completed steps.

## Project Structure

```
.
├── Makefile                  # Build orchestration
├── setup.sh                  # Convenience wrapper
├── boot/
│   ├── boot.S                # 32-bit Multiboot bootloader (→ Long Mode)
│   └── linker.ld             # 32-bit linker script
├── kernel/
│   ├── kernel_main.c         # Kernel entry point
│   ├── entry64.S             # 64-bit assembly entry
│   ├── kernel64.ld           # 64-bit linker script
│   ├── serial.c              # COM1 serial I/O driver
│   ├── timer.c               # TSC timer with PIT calibration
│   ├── memory.c              # Bump allocator
│   ├── panic.c               # Kernel panic handler
│   ├── embedded_files.c      # Embedded file system backend
│   └── kernel.h              # Kernel API header
├── compat/                   # CRuby compatibility layer
│   ├── ruby_on_bare_metal_compat.c       # CRuby VM initialization
│   ├── ruby_on_bare_metal_syscall.c      # Linux syscall emulation (83 syscalls)
│   ├── ruby_on_bare_metal_malloc.c       # Custom memory allocator
│   ├── ruby_on_bare_metal_pthread.c      # Single-threaded pthread stubs
│   └── ruby_on_bare_metal_enc.c          # Static encoding initialization
├── cruby_build/              # CRuby cross-compilation config
│   ├── Makefile              # Cross-build rules
│   └── include/ruby/config.h # CRuby config for Ruby on Bare Metal
├── ruby/scripts/             # Embedded Ruby scripts
│   ├── init.rb               # Main init (shell, VFS, require)
│   └── hello.rb              # Hello world test
├── tools/                    # Build utilities
│   ├── embed_scripts.rb      # Converts .rb files to C byte arrays
│   └── embed_mui.rb          # Embeds mui library files
├── patches/                  # Third-party patches
│   ├── cruby.patch           # CRuby fixes for bare-metal
│   ├── musl.patch            # musl syscall redirection
│   └── mui.patch             # mui bare-metal adaptation
└── third_party/              # External sources (created by make setup)
    ├── cruby/                # CRuby v4.0.2
    ├── musl/                 # musl libc
    └── mui/                  # mui editor
```

## Architecture

```
┌─────────────────────────────────────────────┐
│ mui Editor (97 Ruby files, Vim-like)        │
│ Ruby Shell / REPL (init.rb)                 │
├─────────────────────────────────────────────┤
│ VFS (Ruby Hash)  │  require override        │
├─────────────────────────────────────────────┤
│ CRuby 4.0.2 VM (libruby-static.a)          │
│  + Prism Parser + GC + Regexp (Oniguruma)   │
├─────────────────────────────────────────────┤
│ musl libc (libc_ruby_on_bare_metal.a)       │
│  syscall instruction → ruby_on_bare_metal_syscall() │
├─────────────────────────────────────────────┤
│ Ruby on Bare Metal Kernel (~1,000 lines C)  │
│  serial / timer / memory / syscall / VFS    │
├─────────────────────────────────────────────┤
│ Boot (Multiboot → Long Mode → 64-bit)       │
├─────────────────────────────────────────────┤
│ QEMU (q35, 512MB RAM, serial stdio)         │
└─────────────────────────────────────────────┘
```

### Boot Sequence

Ruby on Bare Metal uses a **two-stage boot process** because QEMU's `-kernel` option only accepts 32-bit Multiboot ELF files, but CRuby requires a 64-bit environment.

1. **Stage 1 (32-bit)**: `boot/boot.S` — Loaded by QEMU via Multiboot. Sets up page tables (512MB identity mapping with 2MB pages), enables SSE/FPU, PAE, and Long Mode, then jumps to the 64-bit kernel.
2. **Stage 2 (64-bit)**: `kernel/entry64.S` → `kernel/kernel_main.c` — Initializes serial output, timer, TLS, memory allocator, then starts the CRuby VM and evaluates the embedded `init.rb`.

### How CRuby Runs Without Linux

musl libc normally issues Linux `syscall` instructions. Ruby on Bare Metal patches musl to call `ruby_on_bare_metal_syscall()` (a C function) instead, which emulates 83 Linux syscalls using the kernel's own serial I/O, memory allocator, timer, and embedded file system. This gives CRuby a familiar libc interface without needing a real Linux kernel.

## Third-party Dependencies

| Library | Version | License | Role |
|---|---|---|---|
| [CRuby](https://github.com/ruby/ruby) | v4.0.2 | [Ruby License](https://www.ruby-lang.org/en/about/license.txt) / [BSD-2-Clause](https://opensource.org/licenses/BSD-2-Clause) / [GPL-2.0](https://www.gnu.org/licenses/gpl-2.0.html) | Ruby interpreter (compiled as static library) |
| [musl libc](https://musl.libc.org/) | latest | [MIT](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT) | Minimal C standard library (with syscall redirection) |
| [mui](https://github.com/S-H-GAMELINKS/mui) | latest | [MIT](https://github.com/S-H-GAMELINKS/mui/blob/main/LICENSE) | Vim-like text editor written in Ruby |

All dependencies are cloned automatically by `make setup`. Their source code is not included in this repository.

## Toolchain

Ruby on Bare Metal uses the **LLVM toolchain** exclusively:

| Tool | Purpose |
|---|---|
| `clang` | C compiler |
| `ld.lld` | Linker |
| `llvm-ar` | Archive tool |
| `llvm-objcopy` | ELF to flat binary conversion |

The LLVM compiler-rt builtins library (`libclang_rt.builtins-x86_64.a`) is also linked for runtime support functions.

## Troubleshooting

### `autoconf: command not found` during `make setup`

CRuby requires autoconf to generate its configure script:

```bash
sudo apt-get install -y autoconf
```

### `ruby: command not found`

A host Ruby is needed for the build tools (`tools/embed_scripts.rb`, etc.):

```bash
sudo apt-get install -y ruby
```

### QEMU shows no output

Make sure you're using `-serial stdio` (already set in the Makefile). If running inside a GUI environment, `-display none` is also set by default to prevent a blank QEMU window.

### Build fails with linker errors about missing symbols

Ensure `make setup` completed successfully. If in doubt, run `make distclean && make setup` to rebuild all dependencies from scratch.

## Development

For a detailed account of the development process, including the bugs encountered and how they were solved, see [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md).

Key files to modify for development:

| Task | File(s) |
|---|---|
| Add system calls | `compat/ruby_on_bare_metal_syscall.c` |
| Change memory layout | `compat/ruby_on_bare_metal_malloc.c`, `boot/boot.S` |
| Add device drivers | `kernel/serial.c`, `kernel/timer.c` |
| Modify boot sequence | `kernel/kernel_main.c`, `kernel/entry64.S` |
| Add Ruby scripts | `ruby/scripts/`, `tools/embed_scripts.rb` |
| Change CRuby config | `cruby_build/include/ruby/config.h` |

## License

This project is licensed under the GNU General Public License v2.0. See the [LICENSE](LICENSE) file for details.

This repository contains only original code and patch files. Third-party dependencies (CRuby, musl libc, mui) are not included in this repository — they are downloaded at build time by `make setup`. When you build the kernel image, the resulting binary links against these libraries statically. Please refer to each dependency's license for redistribution terms of built binaries.

