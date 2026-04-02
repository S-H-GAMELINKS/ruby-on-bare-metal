# CRuby cross-build for Ruby on Bare Metal (bare-metal x86_64)

CC       := clang
AR       := llvm-ar
RANLIB   := llvm-ranlib

RUBY_SRC := ..
HOST_BUILD := ../build_host
MUSL_INSTALL := ../../musl/install

# Compiler flags for freestanding x86_64
CFLAGS := -ffreestanding -fno-builtin -fno-stack-protector -fno-pic \
          -m64 -O1 -Wall -Wno-unused-parameter -Wno-unused-value \
          -Wno-parentheses -Wno-sign-compare -Wno-unused-function \
          -Wno-tautological-compare -Wno-misleading-indentation \
          -Wno-deprecated-declarations -Wno-undef \
          -fno-strict-overflow -fvisibility=hidden \
          -DRUBY_EXPORT -DDISABLE_RUBYGEMS=1 \
          -DDLEXT='".so"' \
          -include malloc.h

# Include paths: musl headers, ruby config, generated files, ruby source
INCLUDES := \
  -nostdinc \
  -isystem $(shell clang -print-resource-dir)/include \
  -isystem $(MUSL_INSTALL)/include \
  -I. \
  -Iinclude \
  -I$(HOST_BUILD) \
  -I$(HOST_BUILD)/.ext/include \
  -I$(RUBY_SRC)/include \
  -I$(RUBY_SRC) \
  -I$(RUBY_SRC)/prism \
  -I$(RUBY_SRC)/enc/unicode/17.0.0

# Ruby source files (same set as miniruby)
RUBY_SRCS := \
  $(RUBY_SRC)/array.c \
  $(RUBY_SRC)/ast.c \
  $(RUBY_SRC)/bignum.c \
  $(RUBY_SRC)/box.c \
  $(RUBY_SRC)/class.c \
  $(RUBY_SRC)/compar.c \
  $(RUBY_SRC)/compile.c \
  $(RUBY_SRC)/complex.c \
  $(RUBY_SRC)/concurrent_set.c \
  $(RUBY_SRC)/cont.c \
  $(RUBY_SRC)/debug.c \
  $(RUBY_SRC)/debug_counter.c \
  $(RUBY_SRC)/dir.c \
  $(RUBY_SRC)/dln_find.c \
  $(RUBY_SRC)/dmydln.c \
  $(RUBY_SRC)/encoding.c \
  $(RUBY_SRC)/enum.c \
  $(RUBY_SRC)/enumerator.c \
  $(RUBY_SRC)/error.c \
  $(RUBY_SRC)/eval.c \
  $(RUBY_SRC)/file.c \
  $(RUBY_SRC)/gc.c \
  $(RUBY_SRC)/hash.c \
  $(RUBY_SRC)/imemo.c \
  $(RUBY_SRC)/inits.c \
  $(RUBY_SRC)/io.c \
  $(RUBY_SRC)/io_buffer.c \
  $(RUBY_SRC)/iseq.c \
  $(RUBY_SRC)/load.c \
  $(RUBY_SRC)/marshal.c \
  $(RUBY_SRC)/math.c \
  $(RUBY_SRC)/memory_view.c \
  $(RUBY_SRC)/builtin.c \
  $(RUBY_SRC)/localeinit.c \
  $(RUBY_SRC)/loadpath.c \
  $(RUBY_SRC)/node.c \
  $(RUBY_SRC)/node_dump.c \
  $(RUBY_SRC)/numeric.c \
  $(RUBY_SRC)/object.c \
  $(RUBY_SRC)/pack.c \
  $(RUBY_SRC)/parser_st.c \
  $(RUBY_SRC)/pathname.c \
  $(RUBY_SRC)/prism_init.c \
  $(RUBY_SRC)/proc.c \
  $(RUBY_SRC)/process.c \
  $(RUBY_SRC)/ractor.c \
  $(RUBY_SRC)/random.c \
  $(RUBY_SRC)/range.c \
  $(RUBY_SRC)/rational.c \
  $(RUBY_SRC)/re.c \
  $(RUBY_SRC)/regcomp.c \
  $(RUBY_SRC)/regenc.c \
  $(RUBY_SRC)/regerror.c \
  $(RUBY_SRC)/regexec.c \
  $(RUBY_SRC)/regparse.c \
  $(RUBY_SRC)/regsyntax.c \
  $(RUBY_SRC)/ruby.c \
  $(RUBY_SRC)/ruby_parser.c \
  $(RUBY_SRC)/scheduler.c \
  $(RUBY_SRC)/set.c \
  $(RUBY_SRC)/shape.c \
  $(RUBY_SRC)/signal.c \
  $(RUBY_SRC)/sprintf.c \
  $(RUBY_SRC)/st.c \
  $(RUBY_SRC)/strftime.c \
  $(RUBY_SRC)/string.c \
  $(RUBY_SRC)/struct.c \
  $(RUBY_SRC)/symbol.c \
  $(RUBY_SRC)/thread.c \
  $(RUBY_SRC)/time.c \
  $(RUBY_SRC)/transcode.c \
  $(RUBY_SRC)/util.c \
  $(RUBY_SRC)/variable.c \
  $(RUBY_SRC)/version.c \
  $(RUBY_SRC)/vm.c \
  $(RUBY_SRC)/vm_backtrace.c \
  $(RUBY_SRC)/vm_dump.c \
  $(RUBY_SRC)/vm_sync.c \
  $(RUBY_SRC)/vm_trace.c \
  $(RUBY_SRC)/weakmap.c \
  $(RUBY_SRC)/missing/setproctitle.c \

# Generated C files from host build
# Note: id.c is #included by symbol.c, lex.c is #included by parse.c
GEN_SRCS := \
  $(HOST_BUILD)/parse.c \
  $(HOST_BUILD)/miniprelude.c \
  $(HOST_BUILD)/enc/trans/newline.c

# Prism parser sources
PRISM_SRCS := \
  $(RUBY_SRC)/prism/api_node.c \
  $(RUBY_SRC)/prism/api_pack.c \
  $(RUBY_SRC)/prism/diagnostic.c \
  $(RUBY_SRC)/prism/encoding.c \
  $(RUBY_SRC)/prism/extension.c \
  $(RUBY_SRC)/prism/node.c \
  $(RUBY_SRC)/prism/options.c \
  $(RUBY_SRC)/prism/pack.c \
  $(RUBY_SRC)/prism/prettyprint.c \
  $(RUBY_SRC)/prism/prism.c \
  $(RUBY_SRC)/prism/regexp.c \
  $(RUBY_SRC)/prism/serialize.c \
  $(RUBY_SRC)/prism/static_literals.c \
  $(RUBY_SRC)/prism/token_type.c \
  $(RUBY_SRC)/prism/util/pm_buffer.c \
  $(RUBY_SRC)/prism/util/pm_char.c \
  $(RUBY_SRC)/prism/util/pm_constant_pool.c \
  $(RUBY_SRC)/prism/util/pm_integer.c \
  $(RUBY_SRC)/prism/util/pm_list.c \
  $(RUBY_SRC)/prism/util/pm_memchr.c \
  $(RUBY_SRC)/prism/util/pm_newline_list.c \
  $(RUBY_SRC)/prism/util/pm_string.c \
  $(RUBY_SRC)/prism/util/pm_strncasecmp.c \
  $(RUBY_SRC)/prism/util/pm_strpbrk.c

# Encoding sources
ENC_SRCS := \
  $(RUBY_SRC)/enc/ascii.c \
  $(RUBY_SRC)/enc/us_ascii.c \
  $(RUBY_SRC)/enc/unicode.c \
  $(RUBY_SRC)/enc/utf_8.c

# Coroutine (pure asm, no OS deps)
CORO_SRC := $(RUBY_SRC)/coroutine/amd64/Context.S

ALL_C_SRCS := $(RUBY_SRCS) $(GEN_SRCS) $(ENC_SRCS)

# Handle name conflicts: prism/ files get prefixed
RUBY_OBJS := $(patsubst %.c,obj/%.o,$(notdir $(ALL_C_SRCS)))
PRISM_OBJS := $(patsubst $(RUBY_SRC)/prism/%.c,obj/prism_%.o,$(PRISM_SRCS))
# Flatten prism/util/ paths: prism/util/pm_buffer.c -> obj/prism_pm_buffer.o
PRISM_OBJS := $(subst obj/prism_util/,obj/prism_,$(PRISM_OBJS))
ALL_OBJS := $(RUBY_OBJS) $(PRISM_OBJS) obj/Context.o

all: libruby-static.a

obj:
	mkdir -p obj

# Generic compilation rule - we compile all .c files into obj/
define compile_rule
obj/$(notdir $(1:.c=.o)): $(1) | obj
	$$(CC) $$(CFLAGS) $$(INCLUDES) -c $$< -o $$@
endef

$(foreach src,$(ALL_C_SRCS),$(eval $(call compile_rule,$(src))))

# Prism sources (prefixed to avoid name conflicts)
obj/prism_%.o: $(RUBY_SRC)/prism/%.c | obj
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Prism util sources
obj/prism_%.o: $(RUBY_SRC)/prism/util/%.c | obj
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Coroutine assembly
obj/Context.o: $(CORO_SRC) | obj
	$(CC) -m64 '-DPREFIXED_SYMBOL(name)=name' -c $< -o $@

# Build static library
libruby-static.a: $(ALL_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

clean:
	rm -rf obj libruby-static.a

.PHONY: all clean
