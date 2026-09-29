# gates_gui_lib — host build (Phase 0: core only, no platform backends).
# Usage: make test          (gcc default)
#        make test CC=clang
# Test binaries land in build/tests/ (never in tests/).

CC      = gcc
CFLAGS  ?= -std=c23 -Wall -Wextra -Werror -g
CPPFLAGS = -Iinclude -Ivendor/proven/include
# Header dependency tracking. Without it, editing an internal header rebuilds
# only some objects and the resulting struct-layout mismatch corrupts memory
# silently (hit on 2026-07-26; see LESSONS.md).
DEPFLAGS = -MMD -MP

BUILD   := build
OBJ     := $(BUILD)/obj
TESTBIN := $(BUILD)/tests

# Compiled proven subset — keep minimal; update vendor/proven/VENDORED.md when it changes.
PROVEN_SRC := \
  vendor/proven/src/proven/memory.c \
  vendor/proven/src/proven/panic.c \
  vendor/proven/src/proven/arena.c \
  vendor/proven/src/proven/heap.c \
  vendor/proven/platform/proven_sys_mem.c

CORE_SRC := \
  src/core/gates_tree.c \
  src/core/gates_widget.c \
  src/core/gates_layout.c \
  src/core/gates_theme.c \
  src/core/gates_paint.c \
  src/core/gates_hit.c \
  src/core/gates_input.c \
  src/core/gates_event.c \
  src/core/gates_textbox.c \
  src/core/gates_clipboard.c \
  src/core/gates_focus.c \
  src/core/gates_command.c \
  src/core/gates_overlay.c \
  src/core/gates_choice.c \
  src/core/gates_form.c \
  src/core/gates_view.c \
  src/core/gates_post.c \
  src/core/gates_timer.c \
  src/core/gates_access.c \
  src/core/gates_version.c \
  src/core/gates_mnemonic.c \
  src/core/gates_menubar.c \
  src/render/gates_draw_list.c \
  src/render/gates_render_soft.c \
  src/text/gates_text_builtin.c \
  src/text/gates_text_utf8.c \
  src/text/gates_text_width.c \
  src/text/gates_text_edit.c

PROVEN_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(PROVEN_SRC))
CORE_OBJ   := $(patsubst %.c,$(OBJ)/%.o,$(CORE_SRC))

TESTS     := test_foundation test_node_pool test_tree test_draw_list test_render_soft \
             test_text_builtin test_layout test_widgets test_paint test_scroll test_split \
             test_text_edit test_textbox test_text_conformance test_ime_compose \
             test_events test_text_editing test_focus_commands \
             test_overlay test_controls test_form test_view test_view_adapters test_post \
             test_post_stress test_theme_dpi test_access test_text_prop test_frame
TEST_BINS := $(addprefix $(TESTBIN)/,$(TESTS))
TEST_OBJ  := $(patsubst %,$(OBJ)/tests/%.o,$(TESTS))

all: $(TEST_BINS)

$(OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(TESTBIN)/%: $(OBJ)/tests/%.o $(CORE_OBJ) $(PROVEN_OBJ)
	@mkdir -p $(TESTBIN)
	$(CC) $(CFLAGS) $^ -lm -o $@

# The threaded posting stress test (T034) alone links pthreads; the shared flags
# stay thread-free. ThreadSanitizer lane:
#   make CC=clang BUILD=build-tsan CFLAGS="-std=c23 -g -O1 -fsanitize=thread" build-tsan/tests/test_post_stress
$(OBJ)/tests/test_post_stress.o: tests/test_post_stress.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(DEPFLAGS) -c $< -o $@

$(TESTBIN)/test_post_stress: $(OBJ)/tests/test_post_stress.o $(CORE_OBJ) $(PROVEN_OBJ)
	@mkdir -p $(TESTBIN)
	$(CC) $(CFLAGS) -pthread $^ -o $@

# Keep test objects: make treats them as intermediates in the .c -> .o -> binary
# chain and would delete (and so recompile) them on every build.
.SECONDARY: $(TEST_OBJ)

-include $(CORE_OBJ:.o=.d) $(PROVEN_OBJ:.o=.d) $(TEST_OBJ:.o=.d)

test: all check-core
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; "$$t"; done
	@echo "all tests passed ($(CC))"

# Core purity: no platform headers anywhere in public headers, core, or renderer.
check-core:
	@! grep -rn -e 'windows\.h' -e 'SDL\.h' -e 'vulkan' -e 'metal' -e 'd3d' include/gates src/core src/render src/text \
		|| { echo "check-core: forbidden platform include found"; exit 1; }
	@echo "check-core: ok"

clean:
	rm -rf $(OBJ) $(TESTBIN) $(BUILD)/win $(BUILD)/lib $(BUILD)/objlib

# ── Win32 lane (mingw-w64 cross build; run where mingw-w64 is installed) ──────
CC_WIN    = x86_64-w64-mingw32-gcc
WINFLAGS  = -std=c23 -Wall -Wextra -Werror -O2
WIN_BUILD = $(BUILD)/win

WIN32_SRC := \
  src/platform/win32/gates_win32_app.c \
  src/platform/win32/gates_win32_window.c \
  src/platform/win32/gates_win32_input.c \
  src/platform/win32/gates_win32_text_gdi.c \
  src/platform/win32/gates_win32_clipboard.c \
  src/platform/win32/gates_win32_uia.c \
  src/platform/win32/gates_win32_perf.c

# Sample applications (0.1.0 release): small complete programs.
SAMPLE_APPS := app_todo app_calculator app_converter app_files

CONTROL_EXAMPLES := ctl_label ctl_button ctl_checkbox ctl_textbox ctl_panel \
                    ctl_stack ctl_split ctl_scroll ctl_keyboard ctl_commands ctl_dialog \
                    ctl_radio ctl_choice ctl_progress ctl_list ctl_table \
                    ctl_tree ctl_log

win: $(WIN_BUILD)/hello_window.exe $(WIN_BUILD)/widgets_demo.exe \
     $(WIN_BUILD)/text_demo.exe $(WIN_BUILD)/text_conformance.exe $(WIN_BUILD)/app_settings.exe \
     $(WIN_BUILD)/app_inspector.exe $(WIN_BUILD)/app_logview.exe \
     $(addprefix $(WIN_BUILD)/,$(addsuffix .exe,$(SAMPLE_APPS))) \
     $(addprefix $(WIN_BUILD)/,$(addsuffix .exe,$(CONTROL_EXAMPLES)))

# Console app (no -mwindows): prints the RFC-0002 8 checklist result.
# ── Libraries and the package (plan-0015) ─────────────────────────────────────
# gates and proven are separate static libraries (owner decision 2026-09-27):
#   host:    libgates_core.a (platform-free core) + libproven.a
#   Windows: libgates.a (core + Win32 backend)    + libproven.a
# A program links -lgates -lproven (Windows) or -lgates_core -lproven (host).
VERSION  := $(shell sed -n 's/^\#define GATES_VERSION_STRING "\(.*\)"/\1/p' include/gates/version.h)
LIBFLAGS  = -std=c23 -Wall -Wextra -Werror -O2
AR       ?= ar
AR_WIN    = x86_64-w64-mingw32-ar
LIB_HOST := $(BUILD)/lib/host
LIB_WIN  := $(BUILD)/lib/win64
LOBJ     := $(BUILD)/objlib
WIN_LIBS  = -lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 -luiautomationcore -lole32 -loleaut32 -luuid
PKG      := dist/gates-$(VERSION)

LIB_HOST_CORE   := $(patsubst %.c,$(LOBJ)/host/%.o,$(CORE_SRC))
LIB_HOST_PROVEN := $(patsubst %.c,$(LOBJ)/host/%.o,$(PROVEN_SRC))
LIB_WIN_GATES   := $(patsubst %.c,$(LOBJ)/win/%.o,$(CORE_SRC) $(WIN32_SRC))
LIB_WIN_PROVEN  := $(patsubst %.c,$(LOBJ)/win/%.o,$(PROVEN_SRC))

$(LOBJ)/host/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(LIBFLAGS) $(DEPFLAGS) -c $< -o $@

$(LOBJ)/win/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC_WIN) $(CPPFLAGS) $(LIBFLAGS) $(DEPFLAGS) -c $< -o $@

$(LIB_HOST)/libgates_core.a: $(LIB_HOST_CORE)
	@mkdir -p $(dir $@)
	rm -f $@ && $(AR) rcs $@ $^

$(LIB_HOST)/libproven.a: $(LIB_HOST_PROVEN)
	@mkdir -p $(dir $@)
	rm -f $@ && $(AR) rcs $@ $^

$(LIB_WIN)/libgates.a: $(LIB_WIN_GATES)
	@mkdir -p $(dir $@)
	rm -f $@ && $(AR_WIN) rcs $@ $^

$(LIB_WIN)/libproven.a: $(LIB_WIN_PROVEN)
	@mkdir -p $(dir $@)
	rm -f $@ && $(AR_WIN) rcs $@ $^

-include $(LIB_HOST_CORE:.o=.d) $(LIB_HOST_PROVEN:.o=.d) $(LIB_WIN_GATES:.o=.d) $(LIB_WIN_PROVEN:.o=.d)

lib-host: $(LIB_HOST)/libgates_core.a $(LIB_HOST)/libproven.a
lib-win: $(LIB_WIN)/libgates.a $(LIB_WIN)/libproven.a
# The Windows part is built where the cross compiler is installed.
HAVE_WIN := $(shell command -v $(CC_WIN) >/dev/null 2>&1 && echo yes)
lib: lib-host $(if $(HAVE_WIN),lib-win)

# The package: headers, libraries, licenses, a consumer example. The proven
# headers shipped are the ones the public gates headers and the shipped proven
# files reach, and nothing more.
PKG_PROVEN_ROOTS = gates/gates.h proven/memory.h proven/panic.h proven/arena.h proven/heap.h
dist: lib
	rm -rf $(PKG)
	mkdir -p $(PKG)/include/gates $(PKG)/include/proven $(PKG)/lib/host $(PKG)/examples/consumer
	cp include/gates/*.h $(PKG)/include/gates/
	for h in $$(for r in $(PKG_PROVEN_ROOTS); do echo "#include <$$r>"; done | \
	            $(CC) -std=c23 $(CPPFLAGS) -M -x c - | tr ' \\' '\n\n' | grep '^vendor/proven/include/proven/'); do \
	    cp $$h $(PKG)/include/proven/; done
	cp $(LIB_HOST)/libgates_core.a $(LIB_HOST)/libproven.a $(PKG)/lib/host/
	$(if $(HAVE_WIN),mkdir -p $(PKG)/lib/win64 && cp $(LIB_WIN)/libgates.a $(LIB_WIN)/libproven.a $(PKG)/lib/win64/)
	cp LICENSE README.md CHANGELOG.md $(PKG)/
	cp vendor/proven/LICENSE $(PKG)/LICENSE-proven
	cp examples/consumer/Makefile examples/consumer/README.md examples/consumer/headless.c \
	   examples/consumer/main.c $(PKG)/examples/consumer/
	cp -R manual manual-ko $(PKG)/

# Windows binaries of every example (release asset), from `make win`.
BIN := dist/gates-$(VERSION)-examples-win64
dist-bin: win
	rm -rf $(BIN)
	mkdir -p $(BIN)
	cp $(WIN_BUILD)/*.exe $(BIN)/
	cp LICENSE $(BIN)/LICENSE.txt
	cp vendor/proven/LICENSE $(BIN)/LICENSE-proven.txt
	cp examples/README.md $(BIN)/EXAMPLES.md
	printf 'gates %s - example programs for Windows 10/11 x64.\r\nNo installation: run any .exe. EXAMPLES.md says what each one shows.\r\nSources: https://github.com/rubidus-api/gates_gui_lib\r\n' "$(VERSION)" > $(BIN)/README.txt

# Copies the package's headers and libraries under PREFIX (include/, lib/).
install: dist
	@test -n "$(PREFIX)" || { echo "usage: make install PREFIX=/some/dir"; exit 1; }
	mkdir -p $(PREFIX)/include $(PREFIX)/lib
	cp -R $(PKG)/include/gates $(PKG)/include/proven $(PREFIX)/include/
	cp $(PKG)/lib/host/*.a $(PREFIX)/lib/
	$(if $(HAVE_WIN),mkdir -p $(PREFIX)/lib/win64 && cp $(PKG)/lib/win64/*.a $(PREFIX)/lib/win64/)

# plan-0017: performance evidence (not a test). Built against the -O2 libraries.
bench: lib-host
	@mkdir -p $(BUILD)/bench
	$(CC) $(CPPFLAGS) $(LIBFLAGS) -pthread tests/bench_core.c -L$(LIB_HOST) -lgates_core -lproven -lm \
		-o $(BUILD)/bench/bench_core
	$(BUILD)/bench/bench_core $(BENCH_ITERS)

# T041: the manual. manual-sync copies every example file into the blocks that print it;
# manual-check fails when a block, the Korean edition or an example build disagrees.
manual-sync:
	sh tests/manual_check.sh sync

manual-check: dist
	CC=$(CC) CC_WIN=$(CC_WIN) sh tests/manual_check.sh check

# T040: headers, version, symbols, package layout, a consumer built from the package alone.
package-check:
	CC=$(CC) CC_WIN=$(CC_WIN) sh tests/test_package.sh

$(WIN_BUILD)/text_conformance.exe: examples/text_conformance/main.c $(CORE_SRC) $(WIN32_SRC) $(PROVEN_SRC)
	@mkdir -p $(WIN_BUILD)
	$(CC_WIN) $(CPPFLAGS) -Itests $(WINFLAGS) $< \
		$(CORE_SRC) $(WIN32_SRC) $(PROVEN_SRC) \
		-lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 \
		-luiautomationcore -lole32 -loleaut32 -luuid -o $@

$(WIN_BUILD)/%.exe: examples/%/main.c $(CORE_SRC) $(WIN32_SRC) $(PROVEN_SRC)
	@mkdir -p $(WIN_BUILD)
	$(CC_WIN) $(CPPFLAGS) $(WINFLAGS) $< \
		$(CORE_SRC) $(WIN32_SRC) $(PROVEN_SRC) \
		-lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 \
		-luiautomationcore -lole32 -loleaut32 -luuid -mwindows -o $@

.PHONY: all test check-core clean win lib lib-host lib-win dist dist-bin install package-check bench manual-sync manual-check
