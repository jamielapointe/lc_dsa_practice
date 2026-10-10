"""Unit and integration tests for LeetCodeClient."""

from typing import Any
from unittest.mock import AsyncMock
from unittest.mock import patch

import httpx
import pytest

from leetcode_mcp.client import LeetCodeClient
from leetcode_mcp.exceptions import LeetCodeNetworkError
from leetcode_mcp.exceptions import PremiumProblemError
from leetcode_mcp.exceptions import ProblemNotFoundError
from leetcode_mcp.exceptions import RateLimitError


class TestInputNormalization:
    """Tests for LeetCodeClient.normalize_input across input variations."""

    @pytest.mark.parametrize(
        ('input_query', 'expected_num'),
        [
            ('2', '2'),
            ('#2', '2'),
            ('LeetCode #2', '2'),
            ('leetcode 2', '2'),
            ('problem #2', '2'),
            ('  # 2  ', '2'),
            (2, '2'),
            (146, '146'),
        ],
    )
    def test_numeric_normalization(self, input_query: str | int, expected_num: str) -> None:
        """Verify numeric inputs with varied prefixes resolve to problem number ID."""
        client = LeetCodeClient()
        slug, num_id = client.normalize_input(input_query)
        assert slug is None
        assert num_id == expected_num

    @pytest.mark.parametrize(
        ('input_url', 'expected_slug'),
        [
            ('https://leetcode.com/problems/add-two-numbers/', 'add-two-numbers'),
            ('http://leetcode.com/problems/add-two-numbers', 'add-two-numbers'),
            ('https://leetcode.com/problems/two-sum/description/', 'two-sum'),
        ],
    )
    def test_url_normalization(self, input_url: str, expected_slug: str) -> None:
        """Verify full problem URLs resolve directly to slug."""
        client = LeetCodeClient()
        slug, num_id = client.normalize_input(input_url)
        assert slug == expected_slug
        assert num_id is None

    @pytest.mark.parametrize(
        ('input_text', 'expected_slug'),
        [
            ('add-two-numbers', 'add-two-numbers'),
            ('Add Two Numbers', 'add-two-numbers'),
            ('Two Sum', 'two-sum'),
            ('Median of Two Sorted Arrays', 'median-of-two-sorted-arrays'),
        ],
    )
    def test_slug_and_title_normalization(self, input_text: str, expected_slug: str) -> None:
        """Verify problem slugs and titles normalize to hyphenated slug."""
        client = LeetCodeClient()
        slug, num_id = client.normalize_input(input_text)
        assert slug == expected_slug
        assert num_id is None

    def test_edge_case_inputs(self) -> None:
        """Verify empty and invalid queries return (None, None)."""
        client = LeetCodeClient()
        assert client.normalize_input('') == (None, None)
        assert client.normalize_input('   ') == (None, None)
        assert client.normalize_input('???') == (None, None)


class TestSlugResolution:
    """Tests for slug resolution via direct matching or GraphQL search."""

    pytestmark = pytest.mark.asyncio

    async def test_resolve_slug_from_direct_slug(self) -> None:
        """Verify slug and URL queries bypass GraphQL search and resolve immediately."""
        client = LeetCodeClient()
        assert await client.resolve_slug('add-two-numbers') == 'add-two-numbers'
        url = 'https://leetcode.com/problems/add-two-numbers/'
        assert await client.resolve_slug(url) == 'add-two-numbers'

    async def test_resolve_slug_from_numeric_id(
        self,
        sample_graphql_question_list_payload: dict[str, Any],
    ) -> None:
        """Verify numeric query executes search and filters exact frontendQuestionId."""

        def handler(request: httpx.Request) -> httpx.Response:
            return httpx.Response(200, json=sample_graphql_question_list_payload)

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        slug = await client.resolve_slug('#2')
        assert slug == 'add-two-numbers'

    async def test_resolve_slug_numeric_not_found(self) -> None:
        """Verify unknown problem number raises ProblemNotFoundError."""

        def handler(request: httpx.Request) -> httpx.Response:
            return httpx.Response(200, json={'data': {'problemsetQuestionList': {'questions': []}}})

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        with pytest.raises(ProblemNotFoundError) as exc_info:
            await client.resolve_slug('99999')
        assert '#99999' in str(exc_info.value)

    async def test_resolve_slug_invalid_input(self) -> None:
        """Verify empty query raises ProblemNotFoundError."""
        client = LeetCodeClient()
        with pytest.raises(ProblemNotFoundError):
            await client.resolve_slug('')


class TestFetchProblemDetails:
    """Tests for LeetCodeClient.fetch_problem with various response scenarios."""

    pytestmark = pytest.mark.asyncio

    async def test_fetch_problem_success_with_mock(
        self,
        sample_graphql_question_list_payload: dict[str, Any],
        sample_graphql_question_detail_payload: dict[str, Any],
    ) -> None:
        """Verify complete problem retrieval and parsing using mock transport."""

        def handler(request: httpx.Request) -> httpx.Response:
            body = request.read().decode()
            if 'problemsetQuestionList' in body:
                return httpx.Response(200, json=sample_graphql_question_list_payload)
            return httpx.Response(200, json=sample_graphql_question_detail_payload)

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        prob = await client.fetch_problem('2')
        assert prob.frontend_id == 2
        assert prob.title == 'Add Two Numbers'
        assert prob.slug == 'add-two-numbers'
        assert prob.difficulty == 'Medium'
        assert prob.is_paid_only is False
        assert 'ListNode' in prob.cpp_snippet
        assert prob.python_snippet == 'class Solution: pass'
        assert len(prob.constraints) == 3
        assert prob.constraints[0] == 'The number of nodes in each linked list is in the range [1, 100].'
        assert prob.constraints[1] == '0 <= Node.val <= 9'
        assert prob.constraints[2] == '1 <= n <= 10^4'
        assert prob.topic_tags == ['Linked List', 'Math', 'Recursion']

    async def test_fetch_problem_not_found(self) -> None:
        """Verify non-existent problem slug raises ProblemNotFoundError."""

        def handler(request: httpx.Request) -> httpx.Response:
            return httpx.Response(200, json={'data': {'question': None}})

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        with pytest.raises(ProblemNotFoundError):
            await client.fetch_problem('non-existent-problem')

    async def test_fetch_problem_premium_locked(self) -> None:
        """Verify premium-only problem raises PremiumProblemError."""

        def handler(request: httpx.Request) -> httpx.Response:
            return httpx.Response(
                200,
                json={'data': {'question': {'title': 'Meeting Rooms II', 'isPaidOnly': True}}},
            )

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        with pytest.raises(PremiumProblemError) as exc_info:
            await client.fetch_problem('meeting-rooms-ii')
        assert 'Premium' in str(exc_info.value)


class TestNetworkResilience:
    """Tests for retry loops, HTTP 429 rate limit backoff, and timeouts."""

    pytestmark = pytest.mark.asyncio

    async def test_retry_on_429_rate_limit_success(
        self,
        sample_graphql_question_detail_payload: dict[str, Any],
    ) -> None:
        """Verify client retries with exponential backoff on HTTP 429 and succeeds."""
        call_count = 0

        def handler(request: httpx.Request) -> httpx.Response:
            nonlocal call_count
            call_count += 1
            if call_count < 3:
                return httpx.Response(429, headers={'Retry-After': '1'})
            return httpx.Response(200, json=sample_graphql_question_detail_payload)

        client = LeetCodeClient(
            transport=httpx.MockTransport(handler),
            max_retries=3,
            base_delay=0.1,
        )
        with patch('asyncio.sleep', new_callable=AsyncMock) as mock_sleep:
            prob = await client.fetch_problem('add-two-numbers')
            assert prob.frontend_id == 2
            assert call_count == 3
            assert mock_sleep.call_count == 2

    async def test_retry_on_429_exhausted(self) -> None:
        """Verify client raises RateLimitError when retries are exhausted."""

        def handler(request: httpx.Request) -> httpx.Response:
            return httpx.Response(429)

        client = LeetCodeClient(
            transport=httpx.MockTransport(handler),
            max_retries=3,
            base_delay=0.01,
        )
        with patch('asyncio.sleep', new_callable=AsyncMock):
            with pytest.raises(RateLimitError) as exc_info:
                await client.fetch_problem('add-two-numbers')
            assert 'HTTP 429' in str(exc_info.value)
            assert isinstance(exc_info.value, LeetCodeNetworkError)

    async def test_retry_on_network_timeout(self) -> None:
        """Verify network timeouts trigger retries and raise LeetCodeNetworkError."""

        def handler(request: httpx.Request) -> httpx.Response:
            raise httpx.ReadTimeout('Connection timed out')

        client = LeetCodeClient(
            transport=httpx.MockTransport(handler),
            max_retries=2,
            base_delay=0.01,
        )
        with patch('asyncio.sleep', new_callable=AsyncMock):
            with pytest.raises(LeetCodeNetworkError):
                await client.fetch_problem('add-two-numbers')

    async def test_client_lifecycle_context_manager(self) -> None:
        """Verify async context manager opens and closes underlying AsyncClient cleanly."""
        async with LeetCodeClient() as client:
            c = await client._get_client()
            assert not c.is_closed
        assert c.is_closed


class TestLiveLeetCodeAPI:
    """Live query tests against external LeetCode GraphQL API."""

    pytestmark = pytest.mark.asyncio

    @pytest.mark.live
    async def test_live_query_problem_2(self) -> None:
        """Perform real GraphQL query against LeetCode for problem #2."""
        async with LeetCodeClient(timeout=15.0) as client:
            prob = await client.fetch_problem('2')
            assert prob.frontend_id == 2
            assert prob.title == 'Add Two Numbers'
            assert prob.slug == 'add-two-numbers'
            assert prob.difficulty == 'Medium'
            assert prob.is_paid_only is False
            assert 'ListNode' in prob.cpp_snippet
            assert len(prob.example_test_cases) >= 3
            assert len(prob.constraints) >= 2
