# Requirements Spec Reference (`docs/systems/<name>.md`)

The spec is the problem statement the user implements against, written the way a software requirements specification is written in an ASPICE (SWE.1)
process: atomic, testable "shall" statements with stable IDs, each traced to verification. Keep it short enough to read in ten minutes.

## Requirement IDs and traceability rules

- ID format: `<PREFIX>-REQ-NNN` (three digits, start at `001`, never renumber; retire an ID instead of reusing it).
- Each requirement is a level-3 heading: `### <PREFIX>-REQ-NNN: <short title>`. `scripts/ci/check_systems_traceability.py` parses exactly this form.
- Each requirement has a `**Verification:**` line whose first word is `Test`, `Analysis`, or `Review`.
  - `Test`: at least one GoogleTest must declare the ID with `RecordProperty("requirements", "<ID>[,<ID>...]")`.
  - `Analysis` / `Review`: verified by inspection (for example "no `new` in the header"); no test required, but say how it is checked.
- Every ID used in a test must exist in a spec. `pixi run check-traceability` enforces both directions; `pixi run check-traceability --matrix <spec>` prints
  the requirement → test matrix for the report (do not hand-maintain a matrix in the spec).

## Writing good requirements

- One behavior per requirement, observable at the public API. "shall return `kWouldBlock` when no unread sample exists", not "should be fast".
- Quantify: capacities, sizes, timeouts, wake-up latency ("within 100 ms"), counts.
- State the error for each invalid input and each resource failure.
- Separate **functional**, **concurrency**, **resource / memory**, **robustness (failure model)**, and **portability** requirements.
- Put design freedom in the spec explicitly ("the implementation may use a process-shared condition variable or bounded polling") so the user knows what
  is required versus what is a choice.
- Mark safety-relevant requirements with a one-line **Rationale** naming the hazard (data corruption, deadlock, unbounded blocking, priority inversion,
  memory exhaustion, torn reads).

## Template

Copy this structure; replace angle-bracket placeholders; delete sections that do not apply.

````markdown
# <Name> — Software Requirements

| Field | Value |
| --- | --- |
| Component | `systems::<Name>` (`src/include/systems/<name>.hpp`) |
| Requirement prefix | `<PREFIX>` |
| Status | Draft (TDD red; implementation pending) |
| Integrity context | Practice component written with ISO 26262 (ASIL-B-style) rigor; not a certified work product |
| Target platforms | Linux, embedded Linux, QNX (production); macOS (development host, see Portability) |

## 1. Problem statement

> <The user's prompt, verbatim.>

## 2. Context and intent

<Two to four sentences: where such a component sits in a robotics / automotive system, who calls it, why the constraints matter.>

## 3. Assumptions and decisions

| ID | Assumption / decision | Alternatives considered |
| --- | --- | --- |
| A-1 | <e.g. Slots have a fixed maximum payload size chosen at creation> | <variable-size records> |

## 4. Glossary

| Term | Meaning |
| --- | --- |
| <Slot> | <One fixed-size block in the ring> |

## 5. Interface overview

<Bullet list of the public types and functions with one line each. The header is authoritative for signatures.>

## 6. Functional requirements

### <PREFIX>-REQ-001: <Title>

The component shall <observable behavior>.

- **Verification:** Test
- **Rationale:** <only for safety-relevant requirements>

## 7. Concurrency requirements

### <PREFIX>-REQ-0NN: <Title>

## 8. Resource and memory requirements

### <PREFIX>-REQ-0NN: No heap allocation after initialization

All operations other than creation / opening shall complete without heap allocation.

- **Verification:** Test (`systems_test::AllocationScope`)
- **Rationale:** Steady-state allocation risks fragmentation, unbounded latency, and memory exhaustion (ISO 26262-6:2018 Table 6 item 1b; MISRA C++:2023
  Rule 21.6.1).

## 9. Robustness and failure-model requirements

### <PREFIX>-REQ-0NN: <Title, e.g. Survives a publisher killed mid-write>

## 10. Portability requirements

### <PREFIX>-REQ-0NN: OS calls are isolated

All operating-system calls shall be confined to the OS adapter translation unit; portable logic shall not include POSIX headers.

- **Verification:** Review

## 11. Failure modes (mini-FMEA)

| Failure mode | Effect | Detection / mitigation | Requirement |
| --- | --- | --- | --- |
| <Writer dies holding the lock> | <Readers and writers deadlock> | <Robust mutex, EOWNERDEAD recovery> | <PREFIX-REQ-0NN> |

## 12. Standards guidance (informative)

| Topic | Reference | How it applies here |
| --- | --- | --- |
| Dynamic memory | ISO 26262-6:2018 Table 6 (1b); MISRA C++:2023 Rule 21.6.1; AUTOSAR C++14 A18-5-1 | Heap only during creation |
| Exceptions | Project policy (see `safety.md`) | `noexcept` API, `std::expected` errors |

Rule numbers are informative pointers; confirm them against licensed copies of the standards before relying on them.

## 13. Out of scope

- <Things the user should not build, e.g. security / access control of the shared-memory name>

## 14. Implementation hints (optional, spoiler-free)

<Concepts worth reading about (seqlock, robust mutex, cache-line padding) without code.>
````
