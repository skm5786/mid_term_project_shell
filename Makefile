# The name of the final executable
TARGET = myterm

OBJDIR = obj

# Add the new source files to the list
# Add history_manager.c to the source list
SOURCES := src/main.c \
           src/gui/x11_window.c \
           src/gui/x11_render.c \
           src/gui/tab_manager.c \
           src/shell/command_parser.c \
           src/shell/command_exec.c \
		   src/shell/redirect_handler.c \
		   src/shell/pipe_handler.c \
		   src/shell/multiwatch.c \
		   src/shell/process_manager.c \
		   src/shell/signal_handler.c \
           src/shell/history_manager.c \
           src/utils/unicode_handler.c \
           src/input/input_handler.c \
		   src/input/line_edit.c \
		   src/input/autocomplete.c
		   
OBJECTS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SOURCES))

# --- Compiler and Flags ---
USE_BASH_MODE ?= 0
CC = gcc
# Add the new include paths
CPPFLAGS = -Isrc/gui -Isrc/shell -Isrc/utils -Isrc/input
CFLAGS = -g -Wall -DUSE_BASH_MODE=$(USE_BASH_MODE)
LDFLAGS =

# --- OS-Specific Settings ---
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CC = clang
    CPPFLAGS += -I/opt/X11/include
    LDFLAGS += -L/opt/X11/lib -lX11
else
    LDFLAGS += -lX11
endif

# --- Rules ---
.PHONY: all
all: $(TARGET)

.PHONY: bash-mode
bash-mode:
	$(MAKE) all USE_BASH_MODE=1

$(TARGET): $(OBJECTS)
	@echo "Linking..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "$(TARGET) created successfully."

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@echo "Cleaning up..."
	rm -f $(TARGET)
	rm -rf $(OBJDIR)

.PHONY: run
run: all
	./$(TARGET)