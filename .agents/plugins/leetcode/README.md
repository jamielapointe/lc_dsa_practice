# LeetCode C++23 Practice Antigravity Plugin & MCP Server

Automates setting up LeetCode DSA practice problems with modern C++23, Google Test, and CMake integration.

## Architecture

This plugin bundles a custom Python MCP server (`leetcode-mcp`) and skills to scaffold and manage LeetCode practice problems in C++23.

```text
.agents/plugins/leetcode/
├── plugin.json         # Antigravity plugin manifest
├── mcp_config.json     # MCP server declaration
├── pyproject.toml      # Isolated Python environment & packaging
├── README.md           # Documentation
├── assets/
│   └── logo.svg        # Plugin logo
├── src/
│   └── leetcode_mcp/
│       ├── __init__.py # Package exports & version
│       ├── __main__.py # Module CLI runner
│       ├── client.py   # Resilient async GraphQL client
│       ├── exceptions.py# Custom domain exceptions
│       ├── models.py   # Pydantic models & file path helpers
│       └── server.py   # Modern MCPServer with get_problem tool
└── tests/
    ├── conftest.py     # Centralized fixtures & mock transports
    ├── test_client.py  # Query normalization, retries & parsing tests
    ├── test_models.py  # Model validation & immutability tests
    └── test_server.py  # Tool registration, schema & invocation tests
```

## Features

- **LeetCode GraphQL Integration**: Connects to `https://leetcode.com/graphql` to resolve problem IDs, titles, slugs, and URLs.
- **Cloudflare Resilience**: Connection pooling with `httpx.AsyncClient`, realistic browser headers, and exponential backoff retry on HTTP 429 and network errors.
- **Mathematical Exponent Preservation**: Converts `<sup>` tags to `^` notation (e.g. `10^4`) to avoid numeric distortion in constraints.
- **HTML to Clean Markdown**: Clean conversion of HTML problem statements using `markdownify`.
- **Modern MCP SDK (v2.2.0+)**: Uses `mcp.server.mcpserver.MCPServer` with strongly-typed tools and automated Pydantic serialization.
- **Strict Stdio Protocol Compliance**: Directs all application and diagnostic logs to `sys.stderr`, preventing corruption of JSON-RPC communication over stdout.

## MCP Tool: `get_problem`

### Input
```json
{
  "problem_query": "2"
}
```
Accepts:
- Numeric ID: `2`, `"2"`, `"#2"`, `"LeetCode #2"`, `"leetcode 2"`, `"problem 2"`
- Slug: `"add-two-numbers"`
- Full URL: `"https://leetcode.com/problems/add-two-numbers/"`
- Title: `"Add Two Numbers"`

### Output Schema (`ProblemDetails`)
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
  "sample_test_case": "[2,4,3]\n[5,6,4]",
  "example_test_cases": ["[2,4,3]\n[5,6,4]", "[0]\n[0]", "[9,9,9,9,9,9,9]\n[9,9,9,9]"],
  "constraints": [
    "The number of nodes in each linked list is in the range [1, 100].",
    "0 <= Node.val <= 9",
    "It is guaranteed that the list represents a number that does not have leading zeros."
  ]
}
```

## Development & Verification

All operations use `uv` and isolate dependencies to the plugin environment:

```bash
# Sync dependencies
uv sync --project .agents/plugins/leetcode

# Run unit tests (mocked, fast)
uv run --project .agents/plugins/leetcode pytest -v -m "not live"

# Code formatting & linting
uv run --project .agents/plugins/leetcode ruff check src tests

# Strict type checking
uv run --project .agents/plugins/leetcode mypy --strict src

# Live integration test against LeetCode GraphQL
uv run --project .agents/plugins/leetcode pytest -v -m "live"
```
