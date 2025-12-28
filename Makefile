# Makefile for piook OOK decoder

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lgpiod

# Target executable
TARGET = piook

# Source files
SRCS = src/piook.c src/decoder.c
OBJS = $(SRCS:.c=.o)

# Header files
HEADERS = src/piook.h

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Compile source files to object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

# Install to system (requires root)
install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Uninstall from system (requires root)
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Run tests (placeholder for future unit tests)
test:
	@echo "No tests implemented yet"

# Show help
help:
	@echo "Available targets:"
	@echo "  all      - Build the executable (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install to /usr/local/bin (requires root)"
	@echo "  uninstall- Remove from /usr/local/bin (requires root)"
	@echo "  test     - Run tests (placeholder)"
	@echo "  help     - Show this help"

.PHONY: all clean install uninstall test help