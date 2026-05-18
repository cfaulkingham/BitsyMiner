# BitsyMiner for ODROID-MC1 Solo

This is a native Linux build of the BitsyMiner lottery miner for the ODROID-MC1 Solo / XU4-class Exynos5422 boards.

The original firmware in `BitsyMinerOpenSource/` is ESP32 Arduino code and uses ESP32-specific WiFi, FreeRTOS, NVS, display, and SHA peripheral APIs. The ODROID build is therefore a separate headless command-line miner.

## Hardware target

The MC1 Solo uses the Samsung Exynos5422 with four Cortex-A7 cores and four faster Cortex-A15 cores. This build defaults to four mining threads pinned to Linux CPUs `4,5,6,7`, which are the Cortex-A15 cores on normal Exynos5422 device trees.

## Build on the ODROID

```sh
sudo apt update
sudo apt install -y build-essential make
make -C odroid odroid-local
```

The binary is written to:

```sh
odroid/build/bitsyminer-odroid-mc1-solo
```

## Cross-compile from another Linux machine

```sh
sudo apt install -y g++-arm-linux-gnueabihf make
make -C odroid odroid
```

Override the compiler if needed:

```sh
make -C odroid odroid ODROID_CXX=/path/to/arm-linux-gnueabihf-g++
```

On macOS, compatible cross compilers are commonly named `arm-unknown-linux-gnueabihf-g++` or `armv7-unknown-linux-gnueabihf-g++`. The Makefile auto-detects these compiler names when they are on `PATH`.

## Run

```sh
./odroid/build/bitsyminer-odroid-mc1-solo \
  --pool stratum+tcp://solo.ckpool.org:3333 \
  --wallet YOUR_BTC_ADDRESS \
  --password x
```

Useful options:

```sh
--threads 8              Use all Exynos5422 cores.
--all-cores              Shortcut for `--threads 8 --core-list 0,1,2,3,4,5,6,7`.
--core-list 4,5,6,7      Set the CPU affinity list used by worker threads.
--no-affinity            Do not pin miner threads.
--suggest-difficulty N   Send `mining.suggest_difficulty` after subscribe.
--benchmark SECONDS      Run a local SHA-256d benchmark without connecting to a pool.
--self-test              Run SHA and header hashing tests.
```

## Notes

This is CPU mining. It is suitable for lottery/solo mining experiments, not profitable Bitcoin mining.
