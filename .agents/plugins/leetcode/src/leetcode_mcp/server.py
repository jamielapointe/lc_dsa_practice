"""LeetCode MCP Server implementation using modern mcp>=2.3.0 MCPServer."""

from typing import TYPE_CHECKING

import structlog
from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from leetcode_mcp.client import LeetCodeClient, configure_logging
from leetcode_mcp.exceptions import LeetCodeError
from leetcode_mcp.models import ProblemDetails


if TYPE_CHECKING:
    from structlog.typing import FilteringBoundLogger


# Enforce structured stderr logging before server instantiation so stdio stdout stays clean.
configure_logging()
logger: FilteringBoundLogger = structlog.get_logger("leetcode_mcp.server")

server = MCPServer(
    name="leetcode",
    version="0.3.0",
    description=(
        "MCP server providing LeetCode problem details, C++, Python 3, and Rust "
        "starter templates, and test cases."
    ),
)
client = LeetCodeClient()


@server.tool(
    name="get_problem",
    description=(
        "Fetch complete LeetCode problem data, C++, Python 3, and Rust code "
        "snippets, test cases, and constraints."
    ),
)
async def get_problem(problem_query: str) -> ProblemDetails:
    """Fetch complete LeetCode problem data, starter snippets, test cases, and constraints.

    Args:
        problem_query: A problem identifier. Supports numeric ID (e.g. '2', '#2', 2),
            URL slug (e.g. 'add-two-numbers'), full LeetCode URL, or title.

    Returns:
        ProblemDetails object containing problem metadata, Markdown description,
        C++/Python 3/Rust template snippets, test cases, constraints, and tags.

    Raises:
        ToolError: If problem is not found, requires LeetCode Premium, or network fails.
    """
    logger.info("executing_get_problem", problem_query=problem_query)
    try:
        return await client.fetch_problem(problem_query)
    except LeetCodeError as err:
        logger.error(
            "get_problem_failed",
            problem_query=problem_query,
            error=str(err),
        )
        raise ToolError(str(err)) from err


def main() -> None:
    """Entrypoint for the LeetCode MCP server running over stdio transport."""
    configure_logging()
    server.run("stdio")


if __name__ == "__main__":
    main()
