# Pottery — Makefile (MSYS2 / MinGW-w64 / GCC)
#
# Prerequisites (MSYS2 MINGW64 shell):
#   pacman -S mingw-w64-x86_64-cairo
#   pacman -S mingw-w64-x86_64-pango
#   pacman -S mingw-w64-x86_64-librsvg
#
# Usage:
#   make          → build examples/hello/hello.exe
#   make clean

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -g \
            -I include \
            -I src \
            -isystem third_party \
            $(shell pkg-config --cflags cairo pango pangocairo librsvg-2.0)

LDFLAGS := $(shell pkg-config --libs cairo pango pangocairo librsvg-2.0) \
            -lgdi32 -luser32
            # -mwindows retiré temporairement pour voir stderr (debug)

SRC_CORE := \
    src/pottery_kiln.c    \
    src/pottery_state.c   \
    src/pottery_renderer.c\
    src/pottery_glaze.c   \
    src/pottery_text.c    \
    src/pottery_svg.c     \
    src/pottery_input.c   \
    src/pottery_vessel.c  \
    src/pottery_statusbar.c\
    src/pottery_toolbar.c  \
    src/molds/pottery_button.c \
    src/molds/pottery_label.c  \
    src/molds/pottery_edit.c   \
    src/molds/pottery_combo.c  \
    src/molds/pottery_list.c   \
    src/molds/pottery_tree.c   \
    src/backends/pottery_win32.c
#   src/molds/pottery_combo.c
#   src/molds/pottery_list.c
#   src/molds/pottery_tree.c

SRC_HELLO := examples/hello/main.c

OBJ_DIR   := build/obj
BIN_DIR   := build/bin

OBJ_CORE  := $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRC_CORE))
OBJ_HELLO := $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRC_HELLO))

TARGET := $(BIN_DIR)/hello.exe

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(OBJ_DIR)/src
	@mkdir -p $(OBJ_DIR)/src/molds
	@mkdir -p $(OBJ_DIR)/src/backends
	@mkdir -p $(OBJ_DIR)/examples/hello

$(OBJ_DIR)/%.o: %.c | dirs
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ_CORE) $(OBJ_HELLO)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf build
