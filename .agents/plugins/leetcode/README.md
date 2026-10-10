# LeetCode Practice Antigravity Plugin (C++23 & Python) and MCP Server

Automates setting up LeetCode DSA practice problems in modern **C++23** (CMake + Google Test) or **Python 3.14** (pytest). The user must tell the skill which
language to use (`cpp` or `python`).

## Architecture

The plugin bundles a custom Python MCP server (`leetcode-mcp`) and the `leetcode-setup` skill.

```text
.agents/plugins/leetcode/
├── plugin.json            # Antigravity plugin manifest
├── mcp_config.json        # MCP server declaration (launched via `pixi run leetcode-mcp`)
├── README.md
├── assets/logo.svg
├── skills/leetcode-setup/
│   ├── SKILL.md           # Language selection + shared workflow
│   └── references/
│       ├── cpp.md         # C++23 / CMake / Google Test templates
│       └── python.md      # Python / pytest templates
├── src/leetcode_mcp/      # MCP server (client.py, models.py, server.py, exceptions.py)
└── tests/                 # Unit tests (mocked) + opt-in live tests (-m live)
```

There is no plugin-local environment: the server, its dependencies, and its tests are part of the repository's root Pixi workspace (`pyproject.toml`).
`PYTHONPATH` is configured by Pixi activation.

## MCP tool: `get_problem`

Input:

```json
{ "problem_query": "2" }
```

Accepts a numeric ID (`2`, `"#2"`, `"LeetCode #2"`), slug (`"add-two-numbers"`), full URL, or title.

Output (`ProblemDetails`):

```json
{
  "frontend_id": 2,
  "question_id": 2,
  "title": "Add Two Numbers",
  "slug": "add-two-numbers",
  "difficulty": "Medium",
  "is_paid_only": false,
  "topic_tags": ["Linked List", "Math", "Recursion"],
  "hints": [],
  "description_markdown": "...",
  "cpp_snippet": "/** ... */ class Solution ...",
  "python_snippet": "class Solution:\n    def addTwoNumbers(...) ...",
  "sample_test_case": "[2,4,3]\n[5,6,4]",
  "example_test_cases": ["[2,4,3]\n[5,6,4]"],
  "constraints": ["The number of nodes in each linked list is in the range [1, 100]."]
}
```

## Features

- LeetCode GraphQL integration with slug/ID/title/URL resolution.
- Cloudflare-resilient client: pooled `httpx.AsyncClient`, browser headers, exponential backoff on HTTP 429/5xx and network errors.
- Mathematical exponent preservation (`<sup>4</sup>` becomes `^4`) and clean HTML to Markdown conversion.
- Both the C++ and Python 3 starter snippets are returned.
- Strict stdio protocol compliance: all logs go to `stderr`.

## Development and verification

Everything runs through Pixi from the repository root:

```bash
pixi install
pixi run pytest .agents/plugins/leetcode/tests          # mocked unit tests (live tests are deselected by default)
pixi run pytest .agents/plugins/leetcode/tests -m live  # live GraphQL integration tests (network required)
pixi run ruff check && pixi run mypy
pixi run leetcode-mcp                                   # start the MCP server over stdio
```
