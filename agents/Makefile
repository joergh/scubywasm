CC := clang

AR := llvm-ar
RANLIB := llvm-ranlib

CLANG_FORMAT := clang-format
CLANG_TIDY   := clang-tidy

LUA_AGENT_FILE ?= lua_agent.lua

WASI_SDK_PATH ?=
WASI_SDK_PATH_STRIPPED := $(strip $(WASI_SDK_PATH))

ifneq ($(WASI_SDK_PATH_STRIPPED),)
WASI_SYSROOT := $(WASI_SDK_PATH_STRIPPED)/share/wasi-sysroot
CC := $(WASI_SDK_PATH_STRIPPED)/bin/clang
AR := $(WASI_SDK_PATH_STRIPPED)/bin/llvm-ar
RANLIB := $(WASI_SDK_PATH_STRIPPED)/bin/llvm-ranlib
CLANG_FORMAT := $(WASI_SDK_PATH_STRIPPED)/bin/clang-format
CLANG_TIDY := $(WASI_SDK_PATH_STRIPPED)/bin/clang-tidy
endif

WARNINGS := \
    -Werror \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Wconversion \
    -Wsign-conversion \
    -Wcast-qual \
    -Wformat=2 \
    -Wundef \
    -Werror=float-equal \
    -Wshadow \
    -Wcast-align \
    -Wunused \
    -Wnull-dereference \
    -Wdouble-promotion \
    -Wimplicit-fallthrough \
    -Werror=strict-prototypes \
    -Wwrite-strings \
    -Wvla

FREESTANDING_CFLAGS := \
    --target=wasm32-unknown-unknown \
    -std=c23 \
    -nostdlib \
    $(WARNINGS) \
    -O3 \
    -ffunction-sections \
    -fdata-sections

FREESTANDING_LDFLAGS := \
    --target=wasm32-unknown-unknown \
    -Wl,--no-entry \
    -Wl,--export-memory \
    -Wl,--gc-sections \
    -Wl,--strip-all \
    -Wl,--export=init_agent \
    -Wl,--export=free_context \
    -Wl,--export=set_config_parameter \
    -Wl,--export=clear_world_state \
    -Wl,--export=update_ship \
    -Wl,--export=update_shot \
    -Wl,--export=update_score \
    -Wl,--export=make_action

WASI_CFLAGS := \
    --target=wasm32-wasi \
    --sysroot=$(WASI_SYSROOT) \
    -std=c23 \
    -O3 \
    -ffunction-sections \
    -fdata-sections \
	-mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=false  # hotfix for setjmp/longjmp

WASI_LDFLAGS := \
    --target=wasm32-wasi \
    --sysroot=$(WASI_SYSROOT) \
    -Wl,--no-entry \
    -Wl,--export-memory \
    -Wl,--gc-sections \
    -Wl,--strip-all \
    -Wl,--export=init_agent \
    -Wl,--export=free_context \
    -Wl,--export=set_config_parameter \
    -Wl,--export=clear_world_state \
    -Wl,--export=update_ship \
    -Wl,--export=update_shot \
    -Wl,--export=update_score \
    -Wl,--export=make_action \
	-Wl,-mllvm,-wasm-enable-sjlj -lsetjmp  # hotfix for setjmp/longjmp

LUA_CPPFLAGS := \
    -I. \
    -Ilua \
    -DLUA_32BITS \
	-Dl_signalT=int

define REQUIRE_WASI_SDK
	@if [ -z "$(WASI_SDK_PATH_STRIPPED)" ]; then \
		echo "ERROR: WASI_SDK_PATH is not set."; \
		echo ""; \
		echo "Example:"; \
		echo "    make WASI_SDK_PATH=/path/to/wasi-sdk/build/install build/lua_agent.wasm"; \
		echo ""; \
		exit 2; \
	fi
endef

.PHONY: all format check-format lint clean

ALL_TARGETS := build/freestanding_agent.wasm
ifneq ($(WASI_SDK_PATH_STRIPPED),)
ALL_TARGETS += build/lua_agent.wasm
endif

all: $(ALL_TARGETS)

build:
	mkdir -p build

build/lua:
	mkdir -p build/lua

build/freestanding_agent.wasm: freestanding_agent.c scubywasm_agent.h | build
	$(CC) $(FREESTANDING_CFLAGS) $(FREESTANDING_LDFLAGS) $< -o $@

LUA_EXCLUDE_C := \
    lua.c \
    luac.c \
    onelua.c \
    ltests.c \
    linit.c \
    liolib.c \
    loslib.c \
    loadlib.c \
    ldblib.c \
    lcorolib.c \
    lstrlib.c \
    lutf8lib.c

LUA_SRC_ALL := $(notdir $(wildcard lua/*.c))
LUA_SRC := $(filter-out $(LUA_EXCLUDE_C),$(LUA_SRC_ALL))
LUA_OBJS := $(patsubst %.c,build/lua/%.o,$(LUA_SRC))

build/lua/%.o: lua/%.c | build/lua
	$(REQUIRE_WASI_SDK)
	$(CC) $(WASI_CFLAGS) $(LUA_CPPFLAGS) -c $< -o $@

build/liblua.a: $(LUA_OBJS) | build
	$(REQUIRE_WASI_SDK)
	$(AR) rcs $@ $(LUA_OBJS)
	$(RANLIB) $@

build/lua_agent.o: lua_agent.c $(LUA_AGENT_FILE) scubywasm_agent.h | build
	$(REQUIRE_WASI_SDK)
	$(CC) $(WARNINGS) $(WASI_CFLAGS) $(LUA_CPPFLAGS) -DLUA_AGENT_FILE=\"$(LUA_AGENT_FILE)\" -c $< -o $@

build/lua_agent.wasm: build/lua_agent.o build/liblua.a | build
	$(REQUIRE_WASI_SDK)
	$(CC) $(WASI_LDFLAGS) build/lua_agent.o build/liblua.a -o $@

format:
	$(CLANG_FORMAT) -Wno-error=unknown -i scubywasm_agent.h
	$(CLANG_FORMAT) -Wno-error=unknown -i freestanding_agent.c
	$(CLANG_FORMAT) -Wno-error=unknown -i lua_agent.c

check-format:
	$(CLANG_FORMAT) -Wno-error=unknown --dry-run --Werror scubywasm_agent.h
	$(CLANG_FORMAT) -Wno-error=unknown --dry-run --Werror freestanding_agent.c
	$(CLANG_FORMAT) -Wno-error=unknown --dry-run --Werror lua_agent.c

build/lint.stamp: freestanding_agent.c $(if $(WASI_SDK_PATH_STRIPPED),lua_agent.c,) | build
	$(CLANG_TIDY) freestanding_agent.c -- -std=c23 $(WARNINGS) --target=wasm32-unknown-unknown -nostdlib -O0
ifneq ($(WASI_SDK_PATH_STRIPPED),)
	$(CLANG_TIDY) lua_agent.c -- -DLUA_AGENT_FILE=\"$(LUA_AGENT_FILE)\" -std=c23 $(WARNINGS) --target=wasm32-wasi --sysroot=$(WASI_SYSROOT) -O0 -I. -Ilua -DLUA_32BITS
endif
	touch $@

lint: build/lint.stamp

clean:
	rm -rf build/
