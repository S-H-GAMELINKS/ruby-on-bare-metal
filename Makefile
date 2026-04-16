CC      := clang
LD      := ld.lld
OBJCOPY := llvm-objcopy
QEMU    := qemu-system-x86_64
NPROC   := $(shell nproc 2>/dev/null || echo 4)

# Third-party locations
MUSL_DIR     := third_party/musl
MUSL_INSTALL := $(MUSL_DIR)/install
CRUBY_SRC    := third_party/cruby
CRUBY_HOST   := $(CRUBY_SRC)/build_host
CRUBY_BUILD  := $(CRUBY_SRC)/build_ruby_on_bare_metal
MUI_DIR      := third_party/mui

# Versions
RUBY_TAG := v4.0.2

# ============================================================
# Flags
# ============================================================

CFLAGS64 := -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -m64 -O2 \
            -Wall -Wextra -Wno-unused-parameter \
            -nostdinc \
            -isystem $(shell $(CC) -print-resource-dir)/include \
            -isystem $(MUSL_INSTALL)/include \
            -I. -Ikernel \
            -I$(CRUBY_BUILD)/include \
            -I$(CRUBY_HOST) \
            -I$(CRUBY_HOST)/.ext/include \
            -I$(CRUBY_SRC)/include \
            -I$(CRUBY_SRC)

LDFLAGS64 := -nostdlib -T kernel/kernel64.ld
LDFLAGS32 := -nostdlib -m elf_i386 -z max-page-size=0x1000 -T boot/linker.ld

COMPILER_RT := $(shell $(CC) -print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a
LIBS := $(CRUBY_BUILD)/libruby-static.a $(MUSL_DIR)/lib/libc_ruby_on_bare_metal.a $(COMPILER_RT)

# ============================================================
# Kernel sources
# ============================================================

K64_C_SRCS := \
  kernel/kernel_main.c \
  kernel/serial.c \
  kernel/panic.c \
  kernel/memory.c \
  kernel/timer.c \
  kernel/embedded_files.c \
  compat/ruby_on_bare_metal_compat.c \
  compat/ruby_on_bare_metal_syscall.c \
  compat/ruby_on_bare_metal_malloc.c \
  compat/ruby_on_bare_metal_pthread.c \
  compat/ruby_on_bare_metal_enc.c

K64_ASM_SRCS := kernel/entry64.S
K64_OBJS := $(K64_C_SRCS:.c=.o) $(K64_ASM_SRCS:.S=.o)
BOOT_OBJS := boot/boot.o
RUBY_SCRIPTS := $(wildcard ruby/scripts/*.rb)

# ============================================================
# Default target — builds everything including dependencies
# ============================================================

all: deps build/kernel.elf

# Ensure third-party libs are built (no-op if already done)
deps: $(MUSL_DIR)/lib/libc_ruby_on_bare_metal.a $(CRUBY_BUILD)/libruby-static.a

$(MUSL_DIR)/lib/libc_ruby_on_bare_metal.a:
	@$(MAKE) setup-musl

$(CRUBY_BUILD)/libruby-static.a:
	@$(MAKE) setup-cruby-cross

# ============================================================
# setup — clone, patch, and build all third-party dependencies
# ============================================================

setup: setup-clone setup-patch setup-musl setup-cruby-host setup-cruby-cross
	@echo ""
	@echo "=== Setup complete ==="
	@echo "Run 'make' to build the kernel, then 'make run' to start."

setup-clone:
	@echo "[setup] Cloning third-party sources..."
	@test -d $(CRUBY_SRC)/.git || \
		git clone --depth 1 --branch $(RUBY_TAG) https://github.com/ruby/ruby.git $(CRUBY_SRC)
	@test -d $(MUSL_DIR)/.git || \
		git clone --depth 1 git://git.musl-libc.org/musl $(MUSL_DIR)
	@test -d $(MUI_DIR)/.git || \
		git clone --depth 1 https://github.com/S-H-GAMELINKS/mui.git $(MUI_DIR)

setup-patch: setup-clone
	@echo "[setup] Applying patches..."
	cd $(CRUBY_SRC) && git checkout -- . 2>/dev/null; git apply ../../patches/cruby.patch
	cd $(MUSL_DIR)  && git checkout -- . 2>/dev/null; git apply ../../patches/musl.patch
	cd $(MUI_DIR)   && git checkout -- . 2>/dev/null; git apply ../../patches/mui.patch
	cp patches/mui_terminal_adapter_ruby_on_bare_metal.rb $(MUI_DIR)/lib/mui/terminal_adapter/ruby_on_bare_metal.rb

setup-musl: setup-patch
	@if [ ! -f $(MUSL_DIR)/lib/libc_ruby_on_bare_metal.a ]; then \
		echo "[setup] Building musl libc..." && \
		cd $(MUSL_DIR) && \
		CC=$(CC) AR=llvm-ar RANLIB=llvm-ranlib \
			./configure --target=x86_64 --prefix=$$(pwd)/install \
			--disable-shared --enable-static && \
		$(MAKE) -j$(NPROC) && $(MAKE) install && \
		cp lib/libc.a lib/libc_ruby_on_bare_metal.a && \
		for obj in $$(llvm-ar t lib/libc.a | grep -E '^(malloc|calloc|realloc|free|memalign|posix_memalign|aligned_alloc|malloc_usable_size|malloc_trim|lite_malloc|expand_heap|mmap|munmap|mprotect|mremap|madvise|pthread_)\.'); do \
			llvm-ar d lib/libc_ruby_on_bare_metal.a "$$obj" 2>/dev/null || true; \
		done && \
		echo "  Created libc_ruby_on_bare_metal.a"; \
	else \
		echo "[setup] musl already built."; \
	fi

setup-cruby-host: setup-clone
	@if [ ! -f $(CRUBY_HOST)/miniruby ]; then \
		echo "[setup] Building CRuby host miniruby..." && \
		mkdir -p $(CRUBY_HOST) && \
		cd $(CRUBY_HOST) && ../autogen.sh && \
		../configure --disable-rubygems --disable-dln --disable-install-doc \
			--disable-yjit --disable-zjit --disable-shared \
			--with-ext="" --with-out-ext="*" && \
		$(MAKE) miniruby -j$(NPROC) && \
		$(MAKE) builtin_binary.rbbin; \
	else \
		echo "[setup] Host miniruby already built."; \
	fi

setup-cruby-cross: setup-musl setup-cruby-host setup-patch
	@mkdir -p $(CRUBY_BUILD)/include/ruby
	@cp cruby_build/Makefile $(CRUBY_BUILD)/Makefile
	@cp cruby_build/include/ruby/config.h $(CRUBY_BUILD)/include/ruby/config.h
	@cp cruby_build/verconf.h $(CRUBY_HOST)/verconf.h
	@if [ ! -f $(CRUBY_BUILD)/libruby-static.a ]; then \
		echo "[setup] Building CRuby cross libruby-static.a..." && \
		$(MAKE) -C $(CRUBY_BUILD) -j$(NPROC); \
	else \
		echo "[setup] Cross libruby-static.a already built."; \
	fi

# ============================================================
# Script embedding
# ============================================================

kernel/generated_scripts.c: $(RUBY_SCRIPTS) tools/embed_scripts.rb
	ruby tools/embed_scripts.rb ruby/scripts/ > $@.tmp
	sed -i '1,2d' $@.tmp
	echo '/* Auto-generated */' > $@
	cat $@.tmp >> $@
	rm $@.tmp

kernel/generated_mui.c: $(MUI_DIR)/lib/mui.rb tools/embed_mui.rb
	ruby tools/embed_mui.rb $(MUI_DIR)/lib/ > $@

kernel/embedded_files.o: kernel/generated_scripts.c kernel/generated_mui.c

# ============================================================
# Kernel build (two-stage)
# ============================================================

build/kernel64.elf: $(K64_OBJS) $(LIBS) kernel/kernel64.ld
	mkdir -p build
	$(LD) $(LDFLAGS64) -o $@ $(K64_OBJS) $(LIBS)

build/kernel64.bin: build/kernel64.elf
	$(OBJCOPY) -O binary $< $@

boot/boot.o: boot/boot.S build/kernel64.bin
	$(CC) -m32 -c $< -o $@

build/kernel.elf: $(BOOT_OBJS) boot/linker.ld
	mkdir -p build
	$(LD) $(LDFLAGS32) -o $@ $(BOOT_OBJS)

%.o: %.c
	$(CC) $(CFLAGS64) -c $< -o $@

kernel/entry64.o: kernel/entry64.S
	$(CC) -m64 -c $< -o $@

# ============================================================
# Run
# ============================================================

run: build/kernel.elf
	$(QEMU) -machine q35 -serial stdio -display none -kernel $< -m 512M

# ============================================================
# WASM build (QEMU compiled to WebAssembly for browser demo)
# ============================================================

QEMU_WASM_DIR  := third_party/qemu-wasm
QEMU_WASM_IMG  := buildqemu-ruby-on-bare-metal

setup-qemu-wasm:
	@test -d $(QEMU_WASM_DIR)/.git || \
		(echo "[wasm] Cloning qemu-wasm..." && \
		 git clone --depth 1 https://github.com/ktock/qemu-wasm.git $(QEMU_WASM_DIR))
	@echo "[wasm] qemu-wasm ready."

patch-qemu-wasm: setup-qemu-wasm
	@echo "[wasm] Applying patches..."
	@if [ -d $(QEMU_WASM_DIR)/subprojects/dtc ] && [ ! -w $(QEMU_WASM_DIR)/subprojects/dtc ]; then \
		echo "[wasm] Fixing permissions on Docker-created files..." && \
		sudo chown -R $$(id -u):$$(id -g) $(QEMU_WASM_DIR)/subprojects/dtc; \
	fi
	cd $(QEMU_WASM_DIR) && git checkout -- . 2>/dev/null; git apply ../../patches/qemu-wasm.patch
	@if [ -f $(QEMU_WASM_DIR)/subprojects/dtc/meson.build ]; then \
		sed -i "s/default_options: 'werror=true'/default_options: 'werror=false'/" \
			$(QEMU_WASM_DIR)/subprojects/dtc/meson.build; \
	fi

build-qemu-wasm: patch-qemu-wasm
	@if [ ! -f web/qemu-system-x86_64.wasm ]; then \
		echo "[wasm] Building Docker image..." && \
		docker build --network=host -t $(QEMU_WASM_IMG) $(QEMU_WASM_DIR) && \
		echo "[wasm] Building QEMU for WASM (this takes a while)..." && \
		docker rm -f build-qemu-wasm 2>/dev/null; \
		docker run -d --name build-qemu-wasm \
			-v $$(pwd)/$(QEMU_WASM_DIR):/qemu \
			$(QEMU_WASM_IMG) sleep infinity && \
		QEMU_EXTRA_CFLAGS="-O3 -g -Wno-error=unused-command-line-argument -matomics -mbulk-memory -DNDEBUG -DG_DISABLE_ASSERT -D_GNU_SOURCE -sASYNCIFY=1 -pthread -sPROXY_TO_PTHREAD=1 -sFORCE_FILESYSTEM -sALLOW_TABLE_GROWTH -sTOTAL_MEMORY=2300MB -sWASM_BIGINT -sMALLOC=mimalloc --js-library=/build/node_modules/xterm-pty/emscripten-pty.js -sEXPORT_ES6=1 -sASYNCIFY_IMPORTS=ffi_call_js" && \
		docker exec build-qemu-wasm emconfigure /qemu/configure \
			--static \
			--target-list=x86_64-softmmu \
			--cpu=wasm32 \
			--cross-prefix= \
			--without-default-features \
			--enable-system \
			--with-coroutine=fiber \
			--enable-virtfs \
			--extra-cflags="$$QEMU_EXTRA_CFLAGS" \
			--extra-cxxflags="$$QEMU_EXTRA_CFLAGS" \
			--extra-ldflags="-sEXPORTED_RUNTIME_METHODS=getTempRet0,setTempRet0,addFunction,removeFunction,TTY,FS" && \
		docker exec build-qemu-wasm emmake make -j$$(nproc) qemu-system-x86_64 && \
		docker stop build-qemu-wasm && \
		echo "[wasm] Extracting build artifacts..." && \
		mkdir -p web && \
		docker cp build-qemu-wasm:/build/qemu-system-x86_64 web/qemu-system-x86_64 && \
		docker cp build-qemu-wasm:/build/qemu-system-x86_64.wasm web/ && \
		docker cp build-qemu-wasm:/build/qemu-system-x86_64.worker.js web/ && \
		docker rm build-qemu-wasm; \
	else \
		echo "[wasm] QEMU WASM already built."; \
	fi

package-wasm: build/kernel.elf build-qemu-wasm
	@echo "[wasm] Packaging kernel + BIOS files..."
	@mkdir -p web/pack
	cp build/kernel.elf web/pack/
	cp $(QEMU_WASM_DIR)/pc-bios/bios-256k.bin web/pack/
	cp $(QEMU_WASM_DIR)/pc-bios/kvmvapic.bin web/pack/
	cp $(QEMU_WASM_DIR)/pc-bios/linuxboot_dma.bin web/pack/
	cp $(QEMU_WASM_DIR)/pc-bios/multiboot_dma.bin web/pack/
	cp $(QEMU_WASM_DIR)/pc-bios/vgabios-stdvga.bin web/pack/
	@echo "[wasm] Packaging complete."

wasm: package-wasm
	@echo ""
	@echo "=== WASM build complete ==="
	@echo "Run 'make wasm-server' to start the dev server."

wasm-server:
	ruby web/server.rb

# ============================================================
# Clean targets
# ============================================================

clean:
	rm -rf build $(K64_OBJS) $(BOOT_OBJS) kernel/generated_scripts.c kernel/generated_mui.c

clean-wasm:
	rm -f web/qemu-system-x86_64 web/qemu-system-x86_64.wasm web/qemu-system-x86_64.worker.js
	rm -rf web/pack
	docker rm -f build-qemu-wasm 2>/dev/null || true

distclean: clean clean-wasm
	rm -rf third_party/cruby third_party/musl third_party/mui third_party/qemu-wasm

.PHONY: all deps setup setup-clone setup-patch setup-musl setup-cruby-host setup-cruby-cross \
        run clean clean-wasm distclean \
        setup-qemu-wasm build-qemu-wasm package-wasm wasm wasm-server
