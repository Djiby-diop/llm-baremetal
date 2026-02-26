CC ?= gcc

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
CPPFLAGS ?= -I.

BUILD_DIR := build

CORE_SRCS := \
	core_hermes/hermes_bus.c \
	core_hermes/hermes_json.c

TEST_SRCS := \
	tests/test_main.c \
	tests/test_hermes_json.c \
	tests/test_bus.c

TEST_BIN := $(BUILD_DIR)/olympe_tests

.PHONY: all test validate clean

all: test

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(TEST_BIN): $(CORE_SRCS) $(TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^

test: $(TEST_BIN)
	./$(TEST_BIN)

validate: test
	./scripts/validate.sh

clean:
	@rm -rf $(BUILD_DIR)
