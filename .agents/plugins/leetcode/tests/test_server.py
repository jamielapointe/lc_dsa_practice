"""Integration tests for MCPServer tool registration, invocation, and stderr logging."""

import json
from unittest.mock import AsyncMock, patch

import pytest
import structlog
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import CallToolResult, TextContent

from leetcode_mcp.client import logger as client_logger
from leetcode_mcp.exceptions import (
    PremiumProblemError,
    ProblemNotFoundError,
    RateLimitError,
)
from leetcode_mcp.models import ProblemDetails
from leetcode_mcp.server import get_problem, main, server


class TestMCPServerToolRegistration:
    """Tests for MCP server tool schemas and metadata."""

    pytestmark = pytest.mark.asyncio

    async def test_tool_registration(self) -> None:
        """Verify get_problem tool is registered with correct parameter schema."""
        tools = await server.list_tools()
        names = [t.name for t in tools]
        assert "get_problem" in names
        tool = next(t for t in tools if t.name == "get_problem")
        assert "problem_query" in tool.input_schema.get("properties", {})
        assert tool.input_schema.get("required") == ["problem_query"]


class TestMCPServerToolInvocation:
    """Tests for MCPServer tool calling, output serialization, and stdio hygiene."""

    pytestmark = pytest.mark.asyncio

    async def test_call_tool_success_via_mock_client(
        self,
        sample_problem_details: ProblemDetails,
    ) -> None:
        """Verify server.call_tool returns CallToolResult with serialized ProblemDetails."""
        target = "leetcode_mcp.server.client.fetch_problem"
        with patch(target, new_callable=AsyncMock) as mock_fetch:
            mock_fetch.return_value = sample_problem_details
            res = await server.call_tool("get_problem", {"problem_query": "2"})
            assert isinstance(res, CallToolResult)
            assert res.is_error is False
            assert res.structured_content is not None
            assert res.structured_content["frontend_id"] == 2
            assert res.structured_content["title"] == "Add Two Numbers"
            first_content = res.content[0]
            assert isinstance(first_content, TextContent)
            parsed = json.loads(first_content.text)
            assert parsed["slug"] == "add-two-numbers"

    async def test_direct_tool_function_invocation(
        self,
        sample_problem_details: ProblemDetails,
    ) -> None:
        """Verify get_problem function can be invoked directly in Python."""
        target = "leetcode_mcp.server.client.fetch_problem"
        with patch(target, new_callable=AsyncMock) as mock_fetch:
            mock_fetch.return_value = sample_problem_details
            res = await get_problem("2")
            assert res.frontend_id == 2
            assert res.title == "Add Two Numbers"

    async def test_tool_error_problem_not_found(self) -> None:
        """Verify ToolError is raised when problem does not exist."""
        target = "leetcode_mcp.server.client.fetch_problem"
        with patch(target, new_callable=AsyncMock) as mock_fetch:
            mock_fetch.side_effect = ProblemNotFoundError("Problem #99999 not found")
            with pytest.raises(ToolError) as exc_info:
                await server.call_tool("get_problem", {"problem_query": "99999"})
            assert "#99999 not found" in str(exc_info.value)

    async def test_tool_error_premium_locked(self) -> None:
        """Verify ToolError is raised when problem requires LeetCode Premium."""
        target = "leetcode_mcp.server.client.fetch_problem"
        with patch(target, new_callable=AsyncMock) as mock_fetch:
            mock_fetch.side_effect = PremiumProblemError("Problem is premium")
            with pytest.raises(ToolError) as exc_info:
                await server.call_tool(
                    "get_problem",
                    {"problem_query": "meeting-rooms-ii"},
                )
            assert "premium" in str(exc_info.value)

    async def test_tool_error_rate_limited(self) -> None:
        """Verify ToolError is raised when rate limited."""
        target = "leetcode_mcp.server.client.fetch_problem"
        with patch(target, new_callable=AsyncMock) as mock_fetch:
            mock_fetch.side_effect = RateLimitError("Rate limit exceeded")
            with pytest.raises(ToolError) as exc_info:
                await server.call_tool("get_problem", {"problem_query": "2"})
            assert "Rate limit exceeded" in str(exc_info.value)

    async def test_structlog_writes_only_to_stderr_never_stdout(
        self,
        capsys: pytest.CaptureFixture[str],
        sample_problem_details: ProblemDetails,
    ) -> None:
        """Verify structlog emits JSON logs strictly to stderr with zero stdout output."""
        cfg = structlog.get_config()
        assert isinstance(cfg["logger_factory"], structlog.PrintLoggerFactory)

        target = "leetcode_mcp.server.client.fetch_problem"
        with patch(target, new_callable=AsyncMock) as mock_fetch:
            mock_fetch.return_value = sample_problem_details
            await get_problem("2")
            client_logger.warning("leetcode_request_retry", status_code=429)
            mock_fetch.side_effect = ProblemNotFoundError("missing")
            with pytest.raises(ToolError):
                await get_problem("99999")

        captured = capsys.readouterr()
        assert not captured.out
        lines = [json.loads(line) for line in captured.err.strip().splitlines()]
        events = [entry["event"] for entry in lines]
        assert "executing_get_problem" in events
        assert "leetcode_request_retry" in events
        assert "get_problem_failed" in events


class TestMCPServerEntrypoint:
    """Tests for MCP server stdio entrypoint."""

    def test_main_runs_stdio_transport(self) -> None:
        """Verify main() launches the MCP server strictly over stdio transport."""
        with patch.object(server, "run") as mock_run:
            main()
        mock_run.assert_called_once_with("stdio")
