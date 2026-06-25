# Host-side tests for the cgba SH4 code emitter.
#
#   make -C tests          # build + run all
#   make -C tests audit    # only the sh-elf-as differential audit
#
# Run from the repository root context; the test sources include the encoder via
# its repo-relative path, so the include root is the parent directory.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra
ROOT    := ..
BIN     := /tmp/cgba-sh4

.PHONY: all smoke audit core thumb clean

all: smoke core thumb audit

smoke: $(ROOT)/tests/sh4_codegen_smoke.c $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_codegen_smoke.c -o $(BIN)-smoke
	$(BIN)-smoke

# Literal-pool + guest reg[] addressing machinery (host-only, no toolchain dep).
core: $(ROOT)/tests/sh4_emit_core_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_emit_core.h \
      $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_emit_core_audit.c -o $(BIN)-core
	$(BIN)-core

# Reference Thumb -> SH4 data-processing translation (host-only).
thumb: $(ROOT)/tests/sh4_thumb_mvp_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_thumb_mvp.h \
       $(ROOT)/ports/fxcg100/sh4/sh4_emit_core.h $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_thumb_mvp_audit.c -o $(BIN)-thumb
	$(BIN)-thumb

# Requires sh-elf-as + sh-elf-objcopy on PATH.
audit: $(ROOT)/tests/sh4_codegen_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_codegen_audit.c -o $(BIN)-audit
	$(BIN)-audit

clean:
	rm -f $(BIN)-smoke $(BIN)-audit
