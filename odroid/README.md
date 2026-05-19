# BitsyMiner native Linux SBC builds

This is a native Linux build of the BitsyMiner lottery miner for small ARM Linux boards.

The original firmware in `BitsyMinerOpenSource/` is ESP32 Arduino code and uses ESP32-specific WiFi, FreeRTOS, NVS, display, and SHA peripheral APIs. These Linux builds are therefore separate headless command-line miners.

## Hardware targets

- ODROID-MC1 Solo / XU4-class Exynos5422 boards: defaults to four threads pinned to Linux CPUs `4,5,6,7`, the Cortex-A15 cores on normal Exynos5422 device trees.
- Orange Pi PC / Allwinner H3 boards: defaults to four threads pinned to Linux CPUs `0,1,2,3`, the H3 Cortex-A7 cores.

## Build on the target board

```sh
sudo apt update
sudo apt install -y build-essential make
make -C odroid odroid-local
```

For Orange Pi PC / H3:

```sh
sudo apt update
sudo apt install -y build-essential make
make -C odroid orangepi-local
```

The binaries are written to:

```sh
odroid/build/bitsyminer-odroid-mc1-solo
odroid/build/bitsyminer-orangepi-pc-h3
```

## Cross-compile from another Linux machine

```sh
sudo apt install -y g++-arm-linux-gnueabihf make
make -C odroid odroid
```

For Orange Pi PC / H3:

```sh
sudo apt install -y g++-arm-linux-gnueabihf make
make -C odroid orangepi
```

Override the compiler if needed:

```sh
make -C odroid odroid ODROID_CXX=/path/to/arm-linux-gnueabihf-g++
make -C odroid orangepi ORANGEPI_CXX=/path/to/arm-linux-gnueabihf-g++
```

On macOS, compatible cross compilers are commonly named `arm-unknown-linux-gnueabihf-g++` or `armv7-unknown-linux-gnueabihf-g++`. The Makefile auto-detects these compiler names when they are on `PATH`.

## NEON builds

Both ARM targets also have an optional four-lane NEON SHA-256d path:

```sh
make -C odroid odroid-neon-local
make -C odroid orangepi-neon-local
```

For cross builds:

```sh
make -C odroid odroid-neon
make -C odroid orangepi-neon
```

NEON binaries are written to:

```sh
odroid/build/bitsyminer-odroid-mc1-solo-neon
odroid/build/bitsyminer-orangepi-pc-h3-neon
```

## Run

```sh
./odroid/build/bitsyminer-odroid-mc1-solo \
  --pool stratum+tcp://solo.ckpool.org:3333 \
  --wallet YOUR_BTC_ADDRESS \
  --password x
```

For Orange Pi PC / H3:

```sh
./odroid/build/bitsyminer-orangepi-pc-h3 \
  --pool stratum+tcp://solo.ckpool.org:3333 \
  --wallet YOUR_BTC_ADDRESS \
  --password x
```

Useful options:

```sh
--threads N              Set the mining thread count.
--all-cores              Use all cores for the compiled target.
--core-list LIST         Set the CPU affinity list used by worker threads.
--no-affinity            Do not pin miner threads.
--suggest-difficulty N   Send `mining.suggest_difficulty` after subscribe.
--benchmark SECONDS      Run a local SHA-256d benchmark without connecting to a pool.
--self-test              Run SHA and header hashing tests.
```

## Allwinner H3 SHA hardware

The Allwinner H3 has a Crypto Engine that advertises SHA-256 support, and mainline Linux can expose it with the `sun8i-ce` crypto driver on suitable kernels. This miner still uses the CPU/NEON SHA loop for mining work because Bitcoin nonce scanning depends on a midstate-optimized hot path, while the Linux crypto API path adds per-hash kernel/DMA setup overhead and does not expose the exact midstate primitive this code uses.

On an Orange Pi PC, check whether the kernel sees the hardware driver with:

```sh
grep -A12 -E 'name *: sha256|driver *: sha256.*sun8i' /proc/crypto
```

If `sha256-sun8i-ce` is present, it may help other system crypto workloads, but it should be benchmarked carefully before replacing the miner's inner loop.

## Notes

This is lottery/solo mining software for experimentation, not profitable Bitcoin mining.
