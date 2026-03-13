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

ifeq ($(OS),Windows_NT)
POWERSHELL ?= powershell

test: $(TEST_BIN)
	@$(POWERSHELL) -NoProfile -Command "$$b=Get-Content -Encoding Byte -TotalCount 4 '$(TEST_BIN)'; $$wsl=Join-Path $$env:WINDIR 'System32\\wsl.exe'; if (-not (Test-Path $$wsl)) { $$wsl=Join-Path $$env:WINDIR 'Sysnative\\wsl.exe' }; if ($$b.Length -ge 4 -and $$b[0] -eq 0x7F -and $$b[1] -eq 0x45 -and $$b[2] -eq 0x4C -and $$b[3] -eq 0x46) { if (Test-Path $$wsl) { & $$wsl bash -lc './$(TEST_BIN)'; exit $$LASTEXITCODE } else { Write-Error 'ELF binary detected on Windows. Install/use WSL, or use a Windows GCC (MinGW) toolchain.'; exit 1 } } else { & '$(TEST_BIN)'; exit $$LASTEXITCODE }"

validate: test
	@$(POWERSHELL) -NoProfile -Command "$$wsl=Join-Path $$env:WINDIR 'System32\\wsl.exe'; if (-not (Test-Path $$wsl)) { $$wsl=Join-Path $$env:WINDIR 'Sysnative\\wsl.exe' }; $$stub=$$env:OLYMPE_ALLOW_MOJO_STUB; if ([string]::IsNullOrWhiteSpace($$stub)) { $$stub='0' }; $$cmd=\"OLYMPE_ALLOW_MOJO_STUB=$$stub ./scripts/validate.sh\"; if (Test-Path $$wsl) { & $$wsl bash -lc $$cmd; exit $$LASTEXITCODE } else { Write-Error 'WSL not found. Install WSL or run validate from a Linux shell.'; exit 1 }"
else
test: $(TEST_BIN)
	./$(TEST_BIN)

validate: test
	./scripts/validate.sh
endif

clean:
	@rm -rf $(BUILD_DIR)
