CXX ?= g++
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
HOST_BIN := $(BUILD_DIR)/bitsyminer-host
ODROID_BIN := $(BUILD_DIR)/bitsyminer-odroid-mc1-solo
ODROID_NEON_BIN := $(BUILD_DIR)/bitsyminer-odroid-mc1-solo-neon
ORANGEPI_BIN := $(BUILD_DIR)/bitsyminer-orangepi-pc-h3
ORANGEPI_NEON_BIN := $(BUILD_DIR)/bitsyminer-orangepi-pc-h3-neon

COMMON_CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -pthread
RELEASE_FLAGS := -O3 -DNDEBUG -fomit-frame-pointer -funroll-loops
ODROID_FLAGS := -mcpu=cortex-a15 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -flto -DBITSY_ODROID_MC1_SOLO=1
ODROID_NEON_FLAGS := $(ODROID_FLAGS) -DBITSY_ENABLE_NEON_SHA=1
ORANGEPI_FLAGS := -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -flto -DBITSY_ORANGE_PI_PC_H3=1
ORANGEPI_NEON_FLAGS := $(ORANGEPI_FLAGS) -DBITSY_ENABLE_NEON_SHA=1

.PHONY: all host odroid odroid-local odroid-neon odroid-neon-local orangepi orangepi-local orangepi-neon orangepi-neon-local self-test clean

all: host

host: $(HOST_BIN)

odroid: $(ODROID_BIN)

odroid-local:
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_FLAGS) -o $(ODROID_BIN) $(SRC)

odroid-neon: $(ODROID_NEON_BIN)

odroid-neon-local:
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_NEON_FLAGS) -o $(ODROID_NEON_BIN) $(SRC)

orangepi: $(ORANGEPI_BIN)

orangepi-local:
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ORANGEPI_FLAGS) -o $(ORANGEPI_BIN) $(SRC)

orangepi-neon: $(ORANGEPI_NEON_BIN)

orangepi-neon-local:
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ORANGEPI_NEON_FLAGS) -o $(ORANGEPI_NEON_BIN) $(SRC)

$(HOST_BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) -O3 -g -o $@ $<

$(ODROID_BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(ODROID_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_FLAGS) -o $@ $<
	@if [ -n "$(ODROID_STRIP)" ]; then $(ODROID_STRIP) --strip-unneeded $@; fi

$(ODROID_NEON_BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(ODROID_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_NEON_FLAGS) -o $@ $<
	@if [ -n "$(ODROID_STRIP)" ]; then $(ODROID_STRIP) --strip-unneeded $@; fi

$(ORANGEPI_BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(ORANGEPI_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ORANGEPI_FLAGS) -o $@ $<
	@if [ -n "$(ORANGEPI_STRIP)" ]; then $(ORANGEPI_STRIP) --strip-unneeded $@; fi

$(ORANGEPI_NEON_BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(ORANGEPI_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ORANGEPI_NEON_FLAGS) -o $@ $<
	@if [ -n "$(ORANGEPI_STRIP)" ]; then $(ORANGEPI_STRIP) --strip-unneeded $@; fi

self-test: $(HOST_BIN)
	$(HOST_BIN) --self-test

clean:
	rm -rf $(BUILD_DIR)
