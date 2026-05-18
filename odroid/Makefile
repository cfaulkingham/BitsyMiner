CXX ?= g++
ODROID_CXX ?= $(shell command -v arm-unknown-linux-gnueabihf-g++ 2>/dev/null || command -v arm-linux-gnueabihf-g++ 2>/dev/null || command -v armv7-unknown-linux-gnueabihf-g++ 2>/dev/null || printf arm-linux-gnueabihf-g++)
ODROID_STRIP ?= $(shell command -v arm-unknown-linux-gnueabihf-strip 2>/dev/null || command -v arm-linux-gnueabihf-strip 2>/dev/null || command -v armv7-unknown-linux-gnueabihf-strip 2>/dev/null || true)

BUILD_DIR := build
SRC := src/bitsyminer_odroid.cpp
HOST_BIN := $(BUILD_DIR)/bitsyminer-host
ODROID_BIN := $(BUILD_DIR)/bitsyminer-odroid-mc1-solo

COMMON_CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -pthread
RELEASE_FLAGS := -O3 -DNDEBUG -fomit-frame-pointer -funroll-loops
ODROID_FLAGS := -mcpu=cortex-a15 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -flto -DBITSY_ODROID_MC1_SOLO=1 -DBITSY_FORCE_NEON=1

.PHONY: all host odroid odroid-local self-test clean

all: host

host: $(HOST_BIN)

odroid: $(ODROID_BIN)

odroid-local:
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_FLAGS) -o $(ODROID_BIN) $(SRC)

$(HOST_BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) -O3 -g -o $@ $<

$(ODROID_BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(ODROID_CXX) $(COMMON_CXXFLAGS) $(RELEASE_FLAGS) $(ODROID_FLAGS) -o $@ $<
	@if [ -n "$(ODROID_STRIP)" ]; then $(ODROID_STRIP) --strip-unneeded $@; fi

self-test: $(HOST_BIN)
	$(HOST_BIN) --self-test

clean:
	rm -rf $(BUILD_DIR)
