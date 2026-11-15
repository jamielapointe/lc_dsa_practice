# Subagent Coordination Protocols in Google Antigravity 2.24.0

## 1. Architectural Philosophy: Division of Cognitive Labor

In Google Antigravity 2.24.0 with Gemini 4 Argon (High Thinking), multi-turn optimization workflows benefit greatly from specialized subagents:

1. **Context Cleanliness**: Micro-optimizing assembly/C++ kernels generates high-density code and compiler diagnostic context. Mixing this with long-form
   LaTeX/academic report writing accelerates context window degradation and compaction.
2. **Specialized Personas**: An agent tuned for microarchitecture (SIMD, cache lines, pipelining) operates best under rigorous, concise systems constraints; an
   agent tuned for academic reporting operates best under formal empirical prose, statistical visualization, and publication standards.
3. **Transactional Safety**: All Git repository mutations (commits, branch checks, clean rollbacks) must remain strictly under the centralized control of the
   Lead Optimizer to prevent race conditions or partial rollbacks.

---

## 2. Multi-Agent Hierarchy & Roles

```text
                        +----------------------------+
                        |       User / System        |
                        +----------------------------+
                                      |
                                      v
                        +----------------------------+
                        |  Lead Optimizer (Root)     |
                        |  - Loop State Machine      |
                        |  - Git Transactions        |
                        |  - Tool & Benchmark Exec   |
                        +----------------------------+
                                      |
                +---------------------+---------------------+
                |                                           |
                v                                           v
+-------------------------------+           +-------------------------------+
|   Kernel Specialist (Argon)   |           |    Academic Scribe Agent      |
|  - SIMD / Bitwise Hypotheses  |           |  - BEST Statistical Synthesis |
|  - Kernel C++ Implementation  |           |  - Empirical Paper Production |
+-------------------------------+           +-------------------------------+
```

### Lead Optimizer (Root Agent)

- **Role**: Master state controller.
- **Responsibilities**:
  - Enforces Preloop, Fast-Screen, Full-Benchmark, and Post-Loop lifecycle.
  - Executes shell commands via `pixi run`.
  - Stages files and creates Git commits (`git commit`) upon successful validation.
  - Executes workspace rollbacks (`git checkout -- . && git clean -fd`) upon failed hypotheses.
  - Dispatches tasks to subagents and reviews their deliverables.

### Academic Scribe Agent (`academic_scribe`)

- **Role**: Empirical researcher and technical documentation specialist.
- **Responsibilities**:
  - Reads raw benchmark telemetry (`data/latest/<experiment>/benchmark_raw.json`).
  - Reads Bayesian summary metrics (`data/latest/<experiment>/analysis.json`).
  - Analyzes git diffs and hypothesis notes.
  - Synthesizes an academic-grade Markdown/LaTeX report (`ACADEMIC_REPORT.md`).
  - Generates ASCII/Markdown comparison tables, microarchitectural breakdown, and statistical interpretations.

---

## 3. Communication Protocol (File + Structured Message)

Antigravity subagents communicate via **File references** and concise **Structured Messages**:

### File Delivery Rule

Large artifacts (benchmark JSONs, statistical summaries, git patches, long-form reports) are **never** pasted verbatim into messages. Instead:

- Data is written to designated filesystem locations (`data/latest/<experiment>/...`).
- Messages transmit the exact absolute file paths.

### Message Schema

```text
**Context**: [Task or milestone context]
**Content**: [File paths, metrics summary, or concise status]
**Action**: [Explicit next action expected from recipient]
```

### Handoff Example: Lead Optimizer -> Academic Scribe

```text
**Context**: Post-loop final reporting for systems::flip_bitmap_unaligned_optimized
**Content**:
- Telemetry: /Users/jamie-lapointe/development/lc_dsa_practice/data/latest/flip_bitmap_unaligned/telemetry.json
- Bayesian Summary: /Users/jamie-lapointe/development/lc_dsa_practice/data/latest/flip_bitmap_unaligned/analysis.json
- Git Diff: /Users/jamie-lapointe/development/lc_dsa_practice/data/latest/flip_bitmap_unaligned/promoted_patch.diff
- Hypotheses Log: /Users/jamie-lapointe/development/lc_dsa_practice/data/latest/flip_bitmap_unaligned/experiment_log.json
**Action**: Synthesize comprehensive Academic Scribe performance analysis report and write to /Users/jamie-lapointe/development/lc_dsa_practice/data/latest/flip_bitmap_unaligned/ACADEMIC_REPORT.md
```

### Handoff Example: Academic Scribe -> Lead Optimizer

```text
**Context**: Academic reporting completed
**Content**: Report written to /Users/jamie-lapointe/development/lc_dsa_practice/data/latest/flip_bitmap_unaligned/ACADEMIC_REPORT.md. Speedup 3.42x, P(better)=99.8%, Cohen's d=2.81, zero test regressions.
**Action**: Review report summary, present interactive review link to user, and complete post-loop phase.
```

---

## 4. Antigravity 2.24.0 SDK Configuration Example

```python
from google.antigravity import Agent, LocalAgentConfig, types

# Configure the Academic Scribe subagent
academic_scribe = types.SubagentConfig(
    name="academic_scribe",
    description="Synthesizes academic-grade empirical performance reports from Bayesian benchmark data.",
    capabilities=types.SubagentCapabilities(
        enabled_tools=[
            types.BuiltinTools.VIEW_FILE,
            types.BuiltinTools.WRITE_TO_FILE,
            types.BuiltinTools.SEND_MESSAGE,
        ],
        agent_behavior=types.AgentBehavior.AUTONOMOUS,
    ),
    system_instructions=(
        "You are an expert Academic Systems Performance Researcher. "
        "You analyze Bayesian BEST distributions, generate formal empirical reports "
        "with LaTeX math, microarchitectural analysis, and structured tables."
    ),
)

# Root agent configuration
root_config = LocalAgentConfig(
    subagents=[academic_scribe],
    capabilities=types.CapabilitiesConfig(
        enable_subagents=True,
        max_subagent_depth=2,
        allowed_subagents=["academic_scribe"],
    ),
)
```
