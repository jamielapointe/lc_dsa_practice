---
name: optimization-loop
description: Orchestrates an agentic hypothesis-driven optimization loop for C++23 systems components using Google Benchmark and Bayesian (BEST + ROPE) performance analysis. Designed for Google Antigravity 2.24.0 and Gemini 4 Argon (High Thinking), featuring fast-screen and full-benchmark phases, automated git clean/commit rollbacks, pre-commit enforcement, and Academic Scribe reporting.
---

# Agentic Optimization Loop Skill (C++23 Systems & Bayesian BEST)

## 🔗 Related Skills

- **cpp-pro**: Modern C++23, low-level micro-optimizations, SIMD intrinsics (NEON, AVX2/512), cache alignment, compiler optimization flags.
- **cmake-pro**: Target-based CMake 4.4.3+, presets, Ninja build configuration, IPO/LTO.
- **python-pro**: Python 3.14 scientific stack (NumPy, SciPy, Plotly) driving Bayesian BEST + ROPE statistical analysis and HTML reports.

---

## Use this skill when

- The user asks to "optimize", "tune", or "accelerate" a C++ systems component (e.g., `systems::flip_bitmap_unaligned_optimized`).
- Conducting empirical performance experiments comparing a candidate implementation against an established baseline.
- Seeking high-confidence statistical verification ($P(\text{better}) \ge 0.95$ via Bayesian BEST with ROPE) before promoting code to main.
- Requiring an autonomous loop that writes hypotheses, benchmarks, self-validates correctness via unit tests, commits improvements atomically, and produces
  academic-grade optimization reports.

## Do not use this skill when

- Scaffolding new LeetCode or systems problems from scratch (use `leetcode-setup` or `systems-setup`).
- Implementing non-performance-critical logic without a Google Benchmark suite.
- Working in an environment without the Pixi toolchain or CMake release presets.

---

## Architecture & Model Optimization (Antigravity 2.24.0 + Gemini 4 Argon)

This skill leverages the deep cognitive capabilities of **Gemini 4 Argon (High Thinking)** running inside **Google Antigravity 2.24.0**:

- **Extended Thinking**: Exploits deep architectural reasoning to formulate non-obvious mechanical hypotheses (cache line layout, SIMD shuffle masks, branchless
  bitwise arithmetic, loop vectorization).
- **Subagent Hierarchies**: Distributes work across specialized personas:
  - **Lead Optimizer (Orchestrator)**: Controls loop state, runs benchmarks, manages Git transactions.
  - **Academic Scribe Agent (`academic_scribe`)**: Generates formal empirical reports, LaTeX equations, Plotly chart annotations, and statistical summaries
    without polluting the implementation context.
- **Strict Empirical Gatekeeping**: Eliminates human benchmark cognitive bias by replacing $p$-values with Kruschke's Bayesian Estimation Supersedes the t-test
  (BEST) coupled with a Region of Practical Equivalence (ROPE).

---

## 📚 Reference Guides

Detailed technical specifications and protocols are available in the `references/` directory:

- [Bayesian BEST + ROPE Mathematical Foundations](references/bayesian_best.md): Kruschke's Student-t model, ROPE thresholding, HDI intervals, and decision
  matrices.
- [Hardware Tuning & Variance Mitigation Guide](references/hardware_tuning.md): CPU frequency governance, core isolation (`isolcpus`), QoS policies, and
  determinism.
- [Subagent Coordination Protocols](references/subagent_coordination.md): Lead Optimizer and Academic Scribe hierarchy, file-first message schema, and
  Antigravity 2.24.0 API configuration.
- [Academic Scribe Agent Guidelines](references/scribe_guidelines.md): Publication-grade empirical report structure, ACM SIGPLAN / IEEE Micro prose, and
  ASCII/LaTeX templates.

---

## Input Parameters

The skill accepts or resolves the following parameters:

| Parameter | Default | Description |
| :--- | :--- | :--- |
| `target_component` | `systems::flip_bitmap_unaligned_optimized` | Fully qualified C++ function or class to optimize |
| `source_file` | `src/cpp/systems/bitmap_flip.cpp` | Path to implementation source file |
| `header_file` | `src/include/systems/bitmap_flip.hpp` | Path to public header file |
| `benchmark_target` | `bitmap_flip_benchmark` | CMake executable target name |
| `benchmark_filter` | `BM_FlipBitmap_.*` | Google Benchmark regex filter |
| `rope_threshold` | `0.02` (2.0%) | Region of Practical Equivalence for Bayesian BEST |
| `fast_screen_reps` | `3` | Iteration count for stage-1 fast screening |
| `full_benchmark_reps`| `30` | Iteration count for stage-2 high-confidence evaluation |
| `confidence_threshold`| `0.95` (95.0%) | Minimum posterior probability $P(\text{better})$ required for baseline promotion |
| `seed` | `0x5EED` | Deterministic random seed for benchmark input generation |

---

## Complete Optimization Workflow

```text
       +-------------------------------------------------------------+
       |                         PRELOOP                             |
       |  1. Deterministic Seeding  2. Host Setup  3. Baseline Run   |
       |  4. Baseline Telemetry (data/baseline/)  5. Initial Hypo    |
       +-------------------------------------------------------------+
                                      |
                                      v
    +===================================================================+
    |                    ITERATIVE OPTIMIZATION LOOP                    |
    |                                                                   |
    |  [1] Check Git Clean  -->  [2] Capture Base Hash (git rev-parse)  |
    |                                   |                               |
    |                                   v                               |
    |  [3] Implement Hypothesis  -->  [4] Static Checks (format/lint)   |
    |                                   |                               |
    |                                   v                               |
    |  [5] Stage-1 Fast Screen (3 reps)                                 |
    |       |--> Slower (>+5%)? ========> REVERT & LOG REGRESSION       |
    |       |--> Neutral / Faster?                                      |
    |                                   |                               |
    |                                   v                               |
    |  [6] Stage-2 Full BEST Benchmark (30 reps)                        |
    |       Compute P(better), P(same), P(worse) vs Baseline            |
    |                                   |                               |
    |                                   v                               |
    |  [7] Evaluate & Act:                                              |
    |       |--> P(better) < 0.95 ========> REVERT & LOG EXPERIMENT     |
    |       |--> P(better) >= 0.95                                      |
    |            - Run Unit Tests (pixi run test-cpp)                   |
    |            - Tests Fail? ========> INVESTIGATE / REVERT           |
    |            - Tests Pass? ========> COMMIT & PROMOTE BASELINE      |
    +===================================================================+
                                      |
                                      v
       +-------------------------------------------------------------+
       |                         POST LOOP                           |
       |  1. Clickable Report Link (file://)  2. Manual User Review   |
       |  3. Baseline Verification  4. Workspace Cleanup             |
       |  5. Academic Scribe Agent Empirical Report                  |
       +-------------------------------------------------------------+
```

---

## Detailed Phase Execution

### Phase 0: Preloop Protocol

1. **Host Environment Stabilization**:
   - Check host variance: on macOS, verify low background load (`ps`, `top`). On Linux/RPi5, apply CPU governor performance mode and core isolation if
     available.
   - Set fixed deterministic seed (`0x5EED`) to ensure identical memory buffers and alignment distributions across all runs.
2. **Execute Initial Baseline Run**:
   - Run the baseline benchmark in full mode (30 repetitions):

     ```bash
     pixi run python scripts/benchmarks/run_benchmark.py \
       --experiment flip_bitmap_unaligned \
       --mode full-statistical \
       --repetitions 30 \
       --filter "BM_FlipBitmap_.*" \
       --rope 0.02
     ```

   - Verify raw telemetry exists: `data/baseline/flip_bitmap_unaligned/benchmark_raw.json` and `telemetry.json`.
   - Calculate baseline Student-$t$ prior parameters ($\mu_{\text{base}}, \sigma_{\text{base}}, \nu_{\text{base}}$).
3. **Formulate Initial Hypothesis Backlog**:
   - With Gemini 4 Argon (High Thinking), analyze the mechanical bottlenecks of the baseline code.
   - Generate a prioritized queue of 3–5 hypotheses detailing:
     - Expected mechanism (e.g. SIMD vectorization, 64-bit word chunking, lookup-table reversal, branch removal).
     - Target code region.
     - Expected impact on CPU time and instruction cache.

---

### Phase 1: Iterative Optimization Loop

For each hypothesis in the backlog, execute the loop steps:

#### Step 1: Clean Git State Check

Verify no dirty uncommitted files exist:

```bash
git status --porcelain
```

*Rule*: If any tracked files are modified, stop immediately. The workspace must be pristine.

#### Step 2: Capture Base Commit Hash

Record the exact base commit hash before modifying code:

```bash
BASE_HASH=$(git rev-parse HEAD)
```

#### Step 3: Implement Hypothesis

- Edit the candidate function (e.g., `systems::flip_bitmap_unaligned_optimized` in `src/cpp/systems/bitmap_flip.cpp`).
- Keep code portable, adhering to modern C++23 standards.

#### Step 4: Formatting, Linting & Pre-commit Gate

Execute all static verification checks:

```bash
pixi run format-cpp
pixi run lint
pixi run format-cmake-check
pixi run check-traceability
pixi run pre-commit
```

*Rule*: If any check fails, fix the code immediately. Do not benchmark unformatted or lint-failing code.

#### Step 5: Stage-1 Fast Screening Benchmark

Run a fast-screen benchmark (3 repetitions) to filter out obvious regressions quickly:

```bash
pixi run python scripts/benchmarks/run_benchmark.py \
  --experiment flip_bitmap_unaligned \
  --mode fast-screen \
  --repetitions 3 \
  --filter "BM_FlipBitmap_.*" \
  --rope 0.02
```

- Compare fast-screen mean time $T_{\text{screen}}$ against baseline mean $T_{\text{base}}$.
- *Fast-Abort Rule*: If $T_{\text{screen}} > 1.05 \times T_{\text{base}}$ (regression $> 5\%$), abort immediately:
  - Record fast-screen regression in `data/latest/flip_bitmap_unaligned/analysis.json`.
  - Revert workspace: `git checkout -- . && git clean -fd`.
  - Proceed to the next hypothesis.

#### Step 6: Stage-2 Full High-Confidence Benchmark & BEST Analysis

If fast screening passes, execute the full 30-repetition benchmark:

```bash
pixi run python scripts/benchmarks/run_benchmark.py \
  --experiment flip_bitmap_unaligned \
  --mode full-statistical \
  --repetitions 30 \
  --filter "BM_FlipBitmap_.*" \
  --rope 0.02
```

Extract key posterior parameters:

- $P(\text{better}) = P(\Delta\mu < -\text{ROPE})$
- $P(\text{same}) = P(-\text{ROPE} \le \Delta\mu \le \text{ROPE})$
- $P(\text{worse}) = P(\Delta\mu > \text{ROPE})$
- 95% Highest Density Interval (HDI) of difference of means $\Delta\mu = \mu_{\text{cand}} - \mu_{\text{base}}$.
- Standardized effect size $\delta$ (Cohen's $d$).

#### Step 7: Evaluate & Act

**Scenario A: Candidate is Statistically Better ($P(\text{better}) \ge 0.95$ and $\Delta\mu < 0$):**

1. Run Unit Tests:

   ```bash
   pixi run test-cpp
   pixi run check-traceability
   ```

2. *Integrity Rule*:
   - If ANY unit test fails, the optimization is invalid.
   - Investigate root cause (e.g. bit-shift edge condition, endianness, unaligned boundary overrun).
   - Under NO circumstances modify test assertions or skip tests to make it pass.
   - If the bug cannot be fixed cleanly without losing performance, treat as failed: revert to `BASE_HASH`.
3. If unit tests pass 100%:
   - Stage ONLY code and documentation changes:

     ```bash
     git add src/include/systems/bitmap_flip.hpp src/cpp/systems/bitmap_flip.cpp docs/systems/bitmap_flip.md
     ```

     *(Never stage `data/` or temporary benchmark outputs!)*
   - Commit to Git:

     ```bash
     git commit -m "perf(systems): optimize flip_bitmap_unaligned_optimized (<hypothesis_slug>)

     - Speedup: X.X% (mean baseline: XX.X ns, candidate: XX.X ns)
     - Bayesian BEST: P(better)=9X.X%, P(same)=X.X%, P(worse)=0.0% (ROPE=2.0%)
     - Effect size: d = X.XX, 95% HDI: [XX.X, XX.X] ns
     - Unit tests: 100% pass (bitmap_flip_test)
     - Clean pre-commit, clang-format, and clang-tidy"
     ```

   - Promote Baseline:

     ```bash
     pixi run python scripts/benchmarks/run_benchmark.py --experiment flip_bitmap_unaligned --promote
     ```

   - Update `BASE_HASH=$(git rev-parse HEAD)`.
   - Log successful experiment in experiment ledger.

**Scenario B: Candidate is Not Better ($P(\text{better}) < 0.95$ or regressed):**

1. Log experiment details (hypothesis, timings, posterior probabilities) into `data/latest/flip_bitmap_unaligned/analysis.json`.
2. Revert workspace cleanly to `BASE_HASH`:

   ```bash
   git checkout -- .
   git clean -fd
   ```

3. Confirm clean state with `git status --porcelain`.

---

### Phase 2: Post Loop Protocol

1. **Present Interactive Review Link**:
   - Provide clickable file link for Chrome / local browser:
     `file:///Users/jamie-lapointe/development/lc_dsa_practice/data/latest/flip_bitmap_unaligned/report.html`
   - Present a Markdown table summarizing all attempted hypotheses, iterations, and BEST metrics.
   - Await user review or confirmation.
2. **Baseline Status & Anomaly Warning**:
   - Confirm baseline in `data/baseline/` reflects latest state.
   - Check for dimension-specific anomalies: if small bitmaps (< 64 bits) regressed while large bitmaps (> 1024 bits) improved, add an explicit warning note
     explaining the multi-regime crossover point.
3. **Workspace Cleanup**:
   - Verify `data/` remains gitignored.
   - Ensure working tree is clean (`git status`).
4. **Academic Scribe Agent Reporting**:
   - Invoke the `academic_scribe` subagent (or adopt persona) to produce a rigorous empirical paper:
     - Abstract & Problem Formulation
     - Hardware Model & Cache Architecture
     - Progressive Optimization Trajectory
     - Bayesian Posterior Analysis (BEST Student-$t$, ROPE, HDI)
     - Microarchitectural Insights (vectorization efficiency, branch retirement)
     - Traceability and Standards Verification

---

## Subagent Coordination Protocol (Antigravity 2.24.0)

When operating with subagents in Antigravity:

- **Lead Optimizer (Orchestrator)**:
  - Manages Git status, file replacement, Pixi build commands, and runner scripts.
  - Keeps core context compact by offloading narrative reporting.
- **Academic Scribe Agent (`academic_scribe`)**:
  - Activated at Post Loop.
  - Inputs: Path to `data/latest/<experiment>/analysis.json`, `data/latest/<experiment>/telemetry.json`, and git diff.
  - Outputs: Formal report (`data/latest/<experiment>/ACADEMIC_REPORT.md` or embedded handoff).
- **Communication Pattern**:

  ```text
  **Context**: Optimization loop completed for systems::flip_bitmap_unaligned_optimized
  **Content**: Telemetry in data/latest/flip_bitmap_unaligned/, baseline promoted, hypotheses tested.
  **Action**: Generate comprehensive Academic Scribe performance analysis report.
  ```
