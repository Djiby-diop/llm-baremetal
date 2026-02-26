CC ?= gcc

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
CPPFLAGS ?= -I.

BUILD_DIR := build

CORE_SRCS := \
	core_hermes/hermes_bus.c \
	core_hermes/hermes_json.c

PILLAR_SRCS := \
	pillars/p01/p01.c \
	pillars/p02/p02.c \
	pillars/p03/p03.c \
	pillars/p04/p04.c \
	pillars/p05/p05.c \
	pillars/p06/p06.c \
	pillars/p07/p07.c \
	pillars/p08/p08.c \
	pillars/p09/p09.c \
	pillars/p10/p10.c

TEST_SRCS := \
	tests/test_main.c \
	tests/test_hermes_json.c \
	tests/test_bus_and_pillars.c

TEST_BIN := $(BUILD_DIR)/olympe_tests

.PHONY: all test clean

all: $(TEST_BIN)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(TEST_BIN): $(CORE_SRCS) $(PILLAR_SRCS) $(TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	@rm -rf $(BUILD_DIR)
