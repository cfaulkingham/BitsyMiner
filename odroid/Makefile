CXX ?= g++
ifeq ($(origin CXX),default)
CXX := $(shell command -v c++ 2>/dev/null || command -v g++ 2>/dev/null || printf g++)
endif

UNAME_S := $(shell uname -s 2>/dev/null || printf unknown)
UNAME_M := $(shell uname -m 2>/dev/null || printf unknown)
NATIVE_ARM_LINUX := $(if $(filter Linux,$(UNAME_S)),$(if $(filter arm% aarch64,$(UNAME_M)),1,))

ifeq ($(NATIVE_ARM_LINUX),1)
ODROID_CXX ?= $(CXX)
ODROID_STRIP ?= $(shell command -v strip 2>/dev/null || true)
ORANGEPI_CXX ?= $(CXX)
ORANGEPI_STRIP ?= $(shell command -v strip 2>/dev/null || true)
else
ODROID_CXX ?= $(shell command -v arm-unknown-linux-gnueabihf-g++ 2>/dev/null || command -v arm-linux-gnueabihf-g++ 2>/dev/null || command -v armv7-unknown-linux-gnueabihf-g++ 2>/dev/null || printf arm-linux-gnueabihf-g++)
ODROID_STRIP ?= $(shell command -v arm-unknown-linux-gnueabihf-strip 2>/dev/null || command -v arm-linux-gnueabihf-strip 2>/dev/null || command -v armv7-unknown-linux-gnueabihf-strip 2>/dev/null || true)
ORANGEPI_CXX ?= $(shell command -v arm-unknown-linux-gnueabihf-g++ 2>/dev/null || command -v arm-linux-gnueabihf-g++ 2>/dev/null || command -v armv7-unknown-linux-gnueabihf-g++ 2>/dev/null || printf arm-linux-gnueabihf-g++)
ORANGEPI_STRIP ?= $(shell command -v arm-unknown-linux-gnueabihf-strip 2>/dev/null || command -v arm-linux-gnueabihf-strip 2>/dev/null || command -v armv7-unknown-linux-gnueabihf-strip 2>/dev/null || true)
endif

BUILD_DIR := build
SRC := src/bitsyminer_odroid.cpp
POOLER_ARM_ASM_SRC := src/pooler_sha2_arm.S
HOST_BIN := $(BUILD_DIR)/bitsyminer-host
ODROID_BIN := $(BUILD_DIR)/bitsyminer-odroid-mc1-solo
ODROID_NEON_BIN := $(BUILD_DIR)/bitsyminer-odroid-mc1-solo-neon
ODROID_POOLER_BIN := $(BUILD_DIR)/bitsyminer-odroid-mc1-solo-pooler
ODROID_POOLER_NEON_BIN := $(BUILD_DIR)/bitsyminer-odroid-mc1-solo-pooler-neon
ORANGEPI_BIN := $(BUILD_DIR)/bitsyminer-orangepi-pc-h3
ORANGEPI_NEON_BIN := $(BUILD_DIR)/bitsyminer-orangepi-pc-h3-neon

COMMON_CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -pthread
RELEASE_FLAGS := -O3 -DNDEBUG -fomit-frame-pointer -funroll-loops
ODROID_FLAGS := -mcpu=cortex-a15 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -flto -DBITSY_ODROID_MC1_SOLO=1
ODROID_NEON_FLAGS := $(ODROID_FLAGS) -DBITSY_ENABLE_NEON_SHA=1
ODROID_POOLER_FLAGS := $(ODROID_FLAGS) -DBITSY_ENABLE_POOLER_ARM_SHA=1
ODROID_POOLER_NEON_FLAGS := $(ODROID_NEON_FLAGS) -DBITSY_ENABLE_POOLER_ARM_SHA=1
ORANGEPI_FLAGS := -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -flto -DBITSY_ORANGE_PI_PC_H3=1
ORANGEPI_NEON_FLAGS := $(ORANGEPI_FLAGS) -DBITSY_ENABLE_NEON_SHA=1

.PHONY: all host odroid odroid-local odroid-neon odroid-neon-local odroid-pooler odroid-pooler-local odroid-pooler-neon odroid-pooler-neon-local orangepi orangepi-local orangepi-neon orangepi-neon-local check-cxx check-odroid-cxx check-orangepi-cxx self-test clean

all: host

host: $(HOST_BIN)

odroid: $(ODROID_BIN)

odroid-local: check-cxx
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_FLAGS) -o $(ODROID_BIN) $(SRC)

odroid-neon: $(ODROID_NEON_BIN)

odroid-neon-local: check-cxx
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_NEON_FLAGS) -o $(ODROID_NEON_BIN) $(SRC)

odroid-pooler: $(ODROID_POOLER_BIN)

odroid-pooler-local: check-cxx
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_POOLER_FLAGS) -o $(ODROID_POOLER_BIN) $(SRC) $(POOLER_ARM_ASM_SRC)

odroid-pooler-neon: $(ODROID_POOLER_NEON_BIN)

odroid-pooler-neon-local: check-cxx
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_POOLER_NEON_FLAGS) -o $(ODROID_POOLER_NEON_BIN) $(SRC) $(POOLER_ARM_ASM_SRC)

orangepi: $(ORANGEPI_BIN)

orangepi-local: check-cxx
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ORANGEPI_FLAGS) -o $(ORANGEPI_BIN) $(SRC)
	@printf '%s\n' "Built scalar Orange Pi binary: $(ORANGEPI_BIN)"
	@printf '%s\n' "Benchmark against the NEON binary before choosing which one to mine with."

orangepi-neon: $(ORANGEPI_NEON_BIN)

orangepi-neon-local: check-cxx
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ORANGEPI_NEON_FLAGS) -o $(ORANGEPI_NEON_BIN) $(SRC)
	@printf '%s\n' "Built NEON Orange Pi binary: $(ORANGEPI_NEON_BIN)"
	@printf '%s\n' "Run it with: ./$(ORANGEPI_NEON_BIN) --self-test"
	@printf '%s\n' "Benchmark against the scalar binary before choosing which one to mine with."

check-cxx:
	@command -v "$(CXX)" >/dev/null 2>&1 || { printf '%s\n' "error: C++ compiler '$(CXX)' was not found. On Debian/Ubuntu, install it with: apt install -y build-essential g++ binutils make"; exit 127; }

check-odroid-cxx:
	@command -v "$(ODROID_CXX)" >/dev/null 2>&1 || { printf '%s\n' "error: ARM hard-float C++ compiler '$(ODROID_CXX)' was not found. Install g++-arm-linux-gnueabihf or set ODROID_CXX=/path/to/compiler."; exit 127; }

check-orangepi-cxx:
	@command -v "$(ORANGEPI_CXX)" >/dev/null 2>&1 || { printf '%s\n' "error: ARM hard-float C++ compiler '$(ORANGEPI_CXX)' was not found. On an Orange Pi, install: apt install -y build-essential g++ binutils make. For cross-builds, install g++-arm-linux-gnueabihf or set ORANGEPI_CXX=/path/to/compiler."; exit 127; }

$(HOST_BIN): $(SRC) | check-cxx
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) -O3 -g -o $@ $<

$(ODROID_BIN): $(SRC) | check-odroid-cxx
	mkdir -p $(BUILD_DIR)
	$(ODROID_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_FLAGS) -o $@ $<
	@if [ -n "$(ODROID_STRIP)" ]; then $(ODROID_STRIP) --strip-unneeded $@; fi

$(ODROID_NEON_BIN): $(SRC) | check-odroid-cxx
	mkdir -p $(BUILD_DIR)
	$(ODROID_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_NEON_FLAGS) -o $@ $<
	@if [ -n "$(ODROID_STRIP)" ]; then $(ODROID_STRIP) --strip-unneeded $@; fi

$(ODROID_POOLER_BIN): $(SRC) $(POOLER_ARM_ASM_SRC) src/cpuminer-config.h | check-odroid-cxx
	mkdir -p $(BUILD_DIR)
	$(ODROID_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_POOLER_FLAGS) -o $@ $(SRC) $(POOLER_ARM_ASM_SRC)
	@if [ -n "$(ODROID_STRIP)" ]; then $(ODROID_STRIP) --strip-unneeded $@; fi

$(ODROID_POOLER_NEON_BIN): $(SRC) $(POOLER_ARM_ASM_SRC) src/cpuminer-config.h | check-odroid-cxx
	mkdir -p $(BUILD_DIR)
	$(ODROID_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_POOLER_NEON_FLAGS) -o $@ $(SRC) $(POOLER_ARM_ASM_SRC)
	@if [ -n "$(ODROID_STRIP)" ]; then $(ODROID_STRIP) --strip-unneeded $@; fi

$(ORANGEPI_BIN): $(SRC) | check-orangepi-cxx
	mkdir -p $(BUILD_DIR)
	$(ORANGEPI_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ORANGEPI_FLAGS) -o $@ $<
	@if [ -n "$(ORANGEPI_STRIP)" ]; then $(ORANGEPI_STRIP) --strip-unneeded $@; fi
	@printf '%s\n' "Built scalar Orange Pi binary: $@"
	@printf '%s\n' "Benchmark against the NEON binary before choosing which one to mine with."

$(ORANGEPI_NEON_BIN): $(SRC) | check-orangepi-cxx
	mkdir -p $(BUILD_DIR)
	$(ORANGEPI_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ORANGEPI_NEON_FLAGS) -o $@ $<
	@if [ -n "$(ORANGEPI_STRIP)" ]; then $(ORANGEPI_STRIP) --strip-unneeded $@; fi
	@printf '%s\n' "Built NEON Orange Pi binary: $@"
	@printf '%s\n' "Run it with: ./$@ --self-test"
	@printf '%s\n' "Benchmark against the scalar binary before choosing which one to mine with."

self-test: $(HOST_BIN)
	$(HOST_BIN) --self-test

clean:
	rm -rf $(BUILD_DIR)
