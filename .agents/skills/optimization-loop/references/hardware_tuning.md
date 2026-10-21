# Hardware Tuning and Variance Mitigation Guide

## 1. Variance in Modern Microbenchmarks

Microbenchmarking low-level operations (such as unaligned bit manipulation executing in tens of nanoseconds) is notoriously susceptible to environmental noise:

1. Dynamic frequency scaling (Intel Turbo Boost, AMD Precision Boost, Apple Silicon DVFS).
2. Heterogeneous CPU architectures (ARM big.LITTLE, Apple P-cores vs. E-cores).
3. Thread migration and context switching across cores.
4. Cold cache vs. warm cache cache-line states.
5. Translation Lookaside Buffer (TLB) misses and Address Space Layout Randomization (ASLR).

To achieve statistical reproducibility required for Bayesian BEST estimation ($CV < 5\%$), the host environment must be stabilized.

---

## 2. macOS (Apple Silicon - M-Series & A-Series Pro) Environment Prep

On macOS running on Apple Silicon (e.g., Apple A18 Pro, M1–M4 series):

### Apple Silicon Scheduler & Clock Architecture

1. **Why `hw.cpufrequency` Is Absent**: Apple removed the Intel-era `hw.cpufrequency` `sysctl` MIB on Apple Silicon because the SoC uses heterogeneous
   Performance (`perflevel0`) and Efficiency (`perflevel1`) clusters managed by the hardware Closed-Loop Performance Controller (CLPC). True maximum cluster
   clock rates live in the IOKit `pmgr` device tree node (`ioreg -c AppleARMIODevice -r -d 1`, keys `voltage-states5-sram` for P-cores and
   `voltage-states1-sram` for E-cores).
2. **Why Mach `THREAD_AFFINITY_POLICY` Returns `KERN_NOT_SUPPORTED`**: The XNU AMP scheduler on ARM64 does not expose fixed core affinity tags to user space;
   instead, it routes threads to Performance vs. Efficiency clusters via Darwin Quality-of-Service (QoS) classes. Furthermore, Google Benchmark's fallback
   frequency estimator reads `cntvct_el0`, which ticks at the fixed 24 MHz crystal reference (`cntfrq_el0 = 24 MHz`) rather than the CPU core clock.
3. **In-Process & Wrapper Mitigations (Built into `bitmap_flip_benchmark` and `DarwinHostTuner`)**:
   - **Thread QoS Pinning**: `bitmap_flip_benchmark` calls `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)` in `main()` before initializing
     Google Benchmark, and `DarwinHostTuner` wraps execution with `taskpolicy -a -t 0 -l 0` and `caffeinate -d -i -m -s -u`.
   - **CLPC Frequency Ramp Warmup**: `bitmap_flip_benchmark` executes a 150 ms tight arithmetic warmup loop immediately after setting
     `QOS_CLASS_USER_INTERACTIVE` so the XNU scheduler migrates the thread to `perflevel0` and locks the P-core PLL at peak frequency (e.g., 4044 MHz on
     Apple A18 Pro) before the first timed repetition begins.
   - **Random Repetition Interleaving**: `run_benchmark.py` enables `--benchmark_enable_random_interleaving=true` and `--benchmark_min_warmup_time` so thermal
     or background drift affects Baseline and Candidate equally.

---

## 3. Linux (x86_64 and aarch64 / Raspberry Pi 5) Prep

When running on dedicated Linux testbeds or edge hardware (e.g. Raspberry Pi 5 with Broadcom BCM2712 quad-core Cortex-A76):

### 1. CPU Frequency Governor

Disable dynamic frequency scaling to prevent CPU clock jitter during warm-up:

```bash
sudo cpupower frequency-set --governor performance
# Or on Debian / Raspberry Pi OS:
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

### 2. Core Isolation & CPU Affinity

Pin the benchmark executable to dedicated, isolated cores (e.g., Cores 1–3, leaving Core 0 for OS interrupts):

```bash
# In /boot/cmdline.txt or GRUB_CMDLINE_LINUX:
# isolcpus=1,2,3 nohz_full=1,2,3 rcu_nocbs=1,2,3

# Run benchmark pinned to Core 2:
taskset -c 2 pixi run python scripts/benchmarks/run_benchmark.py ...
```

### 3. Disable ASLR (Optional for micro-architectural studies)

Address Space Layout Randomization introduces branch predictor and cache line offset shifts between process executions. To eliminate address variance:

```bash
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
```

---

## 4. Benchmark Suite Determinism

Environmental stabilization must be matched by algorithmic determinism:

1. **Deterministic Input Seeding**:
   - Random bitmaps must NEVER use `std::random_device` at benchmark runtime.
   - Use `std::mt19937_64` initialized with a constant seed (`0x5EED` or `--seed=1337`).
2. **Deterministic Alignment Sweeps**:
   - Unaligned bit manipulation benchmarks must systematically evaluate bit offsets $0, 1, 3, 5, 7$ and byte alignments $0, 1, 7, 15, 31, 63$.
   - The test matrix must be generated identically in both baseline and candidate runs.
3. **Cache State Control**:
   - When measuring memory-bound operations, flush or cycle the cache between iterations if cold-cache behavior is desired.
   - When measuring L1/L2 compute throughput, ensure appropriate warm-up iterations (e.g. Google Benchmark's default warm-up phase).
