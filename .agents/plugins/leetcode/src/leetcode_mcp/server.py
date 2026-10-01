"""LeetCode MCP Server implementation using modern mcp>=2.2.0 MCPServer."""

import logging
import sys

from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from leetcode_mcp.client import LeetCodeClient
from leetcode_mcp.exceptions import LeetCodeError
from leetcode_mcp.models import ProblemDetails

# Configure logging strictly to stderr so stdout remains clean for JSON-RPC
logging.basicConfig(
    stream=sys.stderr,
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger("leetcode_mcp.server")

server = MCPServer(
    name="leetcode",
    version="0.1.0",
    description=(
        "MCP server providing LeetCode problem details, C++ starter templates, and test cases."
    ),
)
client = LeetCodeClient()


@server.tool(
    name="get_problem",
    description=(
        "Fetch complete LeetCode problem data, C++ code snippet, test cases, and constraints."
    ),
)
async def get_problem(problem_query: str) -> ProblemDetails:
    """Fetch complete LeetCode problem data, C++ code snippet, test cases, and constraints.

    Args:
        problem_query: A problem identifier. Supports numeric ID (e.g. '2', '#2', 2),
            URL slug (e.g. 'add-two-numbers'), full LeetCode URL, or title.

    Returns:
        ProblemDetails object containing problem metadata, Markdown description,
        C++ template snippet, test cases, constraints, and tags.

    Raises:
        ToolError: If problem is not found, requires LeetCode Premium, or network fails.
    """
    logger.info("Executing tool get_problem with query: %s", problem_query)
    try:
        return await client.fetch_problem(problem_query)
    except LeetCodeError as err:
        logger.error("Failed to fetch problem '%s': %s", problem_query, err)
        raise ToolError(str(err)) from err


def main() -> None:
    """Entrypoint for the LeetCode MCP server running over stdio transport."""
    server.run("stdio")


if __name__ == "__main__":
    main()
