# RT-THM Makefile
CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -std=c99 -I./include
LDFLAGS = -lncurses

# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

# Doxygen
DOXYGEN      = doxygen
DOXYFILE     = Doxyfile
DOXY_OUT_DIR = Documentation/doxygen

# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/worker.c \
       $(SRC_DIR)/ipc_utils.c \
       $(SRC_DIR)/logger.c \
       $(SRC_DIR)/config.c \
       $(SRC_DIR)/signals.c \
       $(SRC_DIR)/UI.c

# Object files
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Target executable
TARGET = $(BIN_DIR)/rt-thm

# Default target
all: directories $(TARGET)

# Create directories
directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

# Link object files into executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✅ Built: $(TARGET)"

# Compile source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "🧹 Cleaned build artifacts"

# Run the program
run: all
	./$(TARGET)

# Check MISRA basics (requires checker.py)
audit:
	@python3 MISRA_compliance/checker.py src/

# Generate Doxygen documentation (works from PowerShell too)
doc:
	@$(DOXYGEN) -v >/dev/null 2>&1 || (echo "❌ ERROR: doxygen not found in PATH" && exit 1)
	@$(DOXYGEN) $(DOXYFILE)
	@echo "📚 Doxygen generated: $(DOXY_OUT_DIR)/html/index.html"

# Open documentation in Windows (PowerShell). Safe if you run make from PowerShell.
doc-open: doc
	@powershell -NoProfile -Command "Start-Process '$(DOXY_OUT_DIR)/html/index.html'"

# Clean generated Doxygen output (PowerShell-safe)
doc-clean:
	@powershell -NoProfile -Command "if (Test-Path '$(DOXY_OUT_DIR)') { Remove-Item -Recurse -Force '$(DOXY_OUT_DIR)' }"
	@echo "🧹 Cleaned Doxygen output"

# Phony targets
.PHONY: all directories clean run audit doc doc-open doc-clean

# Dependencies (simplified - in production use gcc -M)
$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c $(INC_DIR)/project.h $(INC_DIR)/ipc.h $(INC_DIR)/logger.h $(INC_DIR)/config.h $(INC_DIR)/signals.h $(INC_DIR)/worker.h
$(OBJ_DIR)/worker.o: $(SRC_DIR)/worker.c $(INC_DIR)/project.h $(INC_DIR)/worker.h $(INC_DIR)/ipc.h $(INC_DIR)/logger.h $(INC_DIR)/signals.h
$(OBJ_DIR)/ipc_utils.o: $(SRC_DIR)/ipc_utils.c $(INC_DIR)/project.h $(INC_DIR)/ipc.h $(INC_DIR)/logger.h
$(OBJ_DIR)/logger.o: $(SRC_DIR)/logger.c $(INC_DIR)/project.h $(INC_DIR)/logger.h
$(OBJ_DIR)/config.o: $(SRC_DIR)/config.c $(INC_DIR)/project.h $(INC_DIR)/config.h $(INC_DIR)/logger.h
$(OBJ_DIR)/signals.o: $(SRC_DIR)/signals.c $(INC_DIR)/project.h $(INC_DIR)/signals.h $(INC_DIR)/ipc.h $(INC_DIR)/logger.h