"""Unit, property-based, and integration tests for LeetCodeClient."""

import asyncio
import re
from typing import Any, cast
from unittest.mock import AsyncMock, patch

import httpx
import pytest
from hypothesis import given
from hypothesis import strategies as st

from leetcode_mcp.client import LeetCodeClient
from leetcode_mcp.exceptions import (
    LeetCodeNetworkError,
    PremiumProblemError,
    ProblemNotFoundError,
    RateLimitError,
)


_SLUG_RE = re.compile(r"^[a-z0-9-]+$")
_URL_SLUG_STRATEGY = st.from_regex(r"[a-z0-9]+(?:-[a-z0-9]+)*", fullmatch=True)
_ASCII_WORD_STRATEGY = st.from_regex(r"[A-Za-z][A-Za-z0-9]{0,10}", fullmatch=True)
_UNICODE_NOISE_STRATEGY = st.text(
    alphabet=st.sampled_from(["✨", "—", "🚀", "?", "!", "(", ")", ":", "　", "\t"]),
    max_size=5,
)


@st.composite
def _url_and_slug(draw: st.DrawFn) -> tuple[str, str]:
    """Generate a synthetic LeetCode URL and its expected normalized slug."""
    slug = draw(_URL_SLUG_STRATEGY)
    scheme = draw(st.sampled_from(["https://", "http://"]))
    host = draw(st.sampled_from(["leetcode.com", "www.leetcode.com", "LeetCode.COM"]))
    suffix = draw(
        st.sampled_from(["", "/", "/description/", "/solutions/?envType=daily"])
    )
    leading_ws = draw(st.text(alphabet=" \t\n", max_size=4))
    trailing_ws = draw(st.text(alphabet=" \t\n", max_size=4))
    return f"{leading_ws}{scheme}{host}/problems/{slug}{suffix}{trailing_ws}", slug


class TestInputNormalization:
    """Tests for LeetCodeClient.normalize_input across input variations."""

    @pytest.mark.parametrize(
        ("input_query", "expected_num"),
        [
            pytest.param("2", "2", id="str-2"),
            pytest.param("#2", "2", id="hash-2"),
            pytest.param("LeetCode #2", "2", id="leetcode-hash-2"),
            pytest.param("leetcode 2", "2", id="leetcode-space-2"),
            pytest.param("problem #2", "2", id="problem-hash-2"),
            pytest.param("  # 2  ", "2", id="whitespace-hash-2"),
            pytest.param(2, "2", id="int-2"),
            pytest.param(146, "146", id="int-146"),
        ],
    )
    def test_numeric_normalization(
        self,
        input_query: str | int,
        expected_num: str,
    ) -> None:
        """Verify numeric inputs with varied prefixes resolve to problem number ID."""
        client = LeetCodeClient()
        slug, num_id = client.normalize_input(input_query)
        assert slug is None
        assert num_id == expected_num

    @pytest.mark.parametrize(
        ("input_url", "expected_slug"),
        [
            ("https://leetcode.com/problems/add-two-numbers/", "add-two-numbers"),
            ("http://leetcode.com/problems/add-two-numbers", "add-two-numbers"),
            ("https://leetcode.com/problems/two-sum/description/", "two-sum"),
        ],
    )
    def test_url_normalization(self, input_url: str, expected_slug: str) -> None:
        """Verify full problem URLs resolve directly to slug."""
        client = LeetCodeClient()
        slug, num_id = client.normalize_input(input_url)
        assert slug == expected_slug
        assert num_id is None

    @pytest.mark.parametrize(
        ("input_text", "expected_slug"),
        [
            ("add-two-numbers", "add-two-numbers"),
            ("Add Two Numbers", "add-two-numbers"),
            ("Two Sum", "two-sum"),
            ("Median of Two Sorted Arrays", "median-of-two-sorted-arrays"),
        ],
    )
    def test_slug_and_title_normalization(
        self,
        input_text: str,
        expected_slug: str,
    ) -> None:
        """Verify problem slugs and titles normalize to hyphenated slug."""
        client = LeetCodeClient()
        slug, num_id = client.normalize_input(input_text)
        assert slug == expected_slug
        assert num_id is None

    def test_edge_case_inputs(self) -> None:
        """Verify empty and invalid queries return (None, None)."""
        client = LeetCodeClient()
        assert client.normalize_input("") == (None, None)
        assert client.normalize_input("   ") == (None, None)
        assert client.normalize_input("???") == (None, None)

    @pytest.mark.unit
    @given(url_case=_url_and_slug())
    def test_url_normalization_property(self, url_case: tuple[str, str]) -> None:
        """Fuzz LeetCode problem URLs and verify exact slug extraction."""
        url, expected_slug = url_case
        extracted_slug, num_id = LeetCodeClient.normalize_input(url)
        assert extracted_slug == expected_slug
        assert num_id is None

    @pytest.mark.unit
    @given(
        num=st.integers(min_value=1, max_value=100_000),
        prefix=st.sampled_from([
            "",
            "#",
            "# ",
            "LeetCode #",
            "leetcode ",
            "problem #",
            "Problem ",
            "LeetCode Problem #",
        ]),
        leading_ws=st.text(alphabet=" \t", max_size=4),
        trailing_ws=st.text(alphabet=" \t", max_size=4),
    )
    def test_numeric_normalization_property(
        self,
        num: int,
        prefix: str,
        leading_ws: str,
        trailing_ws: str,
    ) -> None:
        """Fuzz integer and prefixed numeric queries and verify exact ID extraction."""
        assert LeetCodeClient.normalize_input(num) == (None, str(num))
        formatted = f"{leading_ws}{prefix}{num}{trailing_ws}"
        assert LeetCodeClient.normalize_input(formatted) == (None, str(num))

    @pytest.mark.unit
    @given(
        words=st.lists(_ASCII_WORD_STRATEGY, min_size=1, max_size=5),
        sep=st.sampled_from([" ", "  ", "_", "__", "-"]),
        noise_left=_UNICODE_NOISE_STRATEGY,
        noise_right=_UNICODE_NOISE_STRATEGY,
    )
    def test_unicode_title_slug_normalization_property(
        self,
        words: list[str],
        sep: str,
        noise_left: str,
        noise_right: str,
    ) -> None:
        """Fuzz whitespace, underscores, and Unicode noise around ASCII title tokens."""
        query = f"{noise_left} {sep.join(words)} {noise_right}"
        slug, num_id = LeetCodeClient.normalize_input(query)
        expected_parts = [w.lower() for part in words for w in part.split()]
        # Words starting with digits alone are only numeric if a single pure-digit token.
        joined = "-".join(expected_parts)
        assert slug == joined
        assert num_id is None

    @pytest.mark.unit
    @given(text=st.text(max_size=80))
    def test_arbitrary_unicode_input_invariants(self, text: str) -> None:
        """Fuzz arbitrary Unicode text to guarantee output invariants and zero ReDoS."""
        slug, num_id = LeetCodeClient.normalize_input(text)
        assert not (slug is not None and num_id is not None)
        if slug is not None:
            assert slug
            assert _SLUG_RE.match(slug) is not None
            assert not slug.startswith("-")
            assert not slug.endswith("-")
        if num_id is not None:
            assert num_id
            assert num_id.isascii()
            assert num_id.isdigit()


class TestSlugResolution:
    """Tests for slug resolution via direct matching or GraphQL search."""

    pytestmark = pytest.mark.asyncio

    async def test_resolve_slug_from_direct_slug(self) -> None:
        """Verify slug and URL queries bypass GraphQL search and resolve immediately."""
        client = LeetCodeClient()
        assert await client.resolve_slug("add-two-numbers") == "add-two-numbers"
        url = "https://leetcode.com/problems/add-two-numbers/"
        assert await client.resolve_slug(url) == "add-two-numbers"

    async def test_resolve_slug_from_numeric_id(
        self,
        sample_graphql_question_list_payload: dict[str, Any],
    ) -> None:
        """Verify numeric query executes search and filters exact frontendQuestionId."""

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            return httpx.Response(200, json=sample_graphql_question_list_payload)

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        slug = await client.resolve_slug("#2")
        assert slug == "add-two-numbers"

    async def test_resolve_slug_numeric_not_found(self) -> None:
        """Verify unknown problem number raises ProblemNotFoundError."""

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            return httpx.Response(
                200,
                json={"data": {"problemsetQuestionList": {"questions": []}}},
            )

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        with pytest.raises(ProblemNotFoundError) as exc_info:
            await client.resolve_slug("99999")
        assert "#99999" in str(exc_info.value)

    async def test_resolve_slug_invalid_input(self) -> None:
        """Verify empty query raises ProblemNotFoundError."""
        client = LeetCodeClient()
        with pytest.raises(ProblemNotFoundError):
            await client.resolve_slug("")


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
            if "problemsetQuestionList" in body:
                return httpx.Response(200, json=sample_graphql_question_list_payload)
            return httpx.Response(200, json=sample_graphql_question_detail_payload)

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        prob = await client.fetch_problem("2")
        assert prob.frontend_id == 2
        assert prob.title == "Add Two Numbers"
        assert prob.slug == "add-two-numbers"
        assert prob.difficulty == "Medium"
        assert prob.is_paid_only is False
        assert "ListNode" in prob.cpp_snippet
        assert prob.python_snippet == "class Solution: pass"
        assert "pub fn add_two_numbers" in prob.rust_snippet
        assert len(prob.constraints) == 3
        assert (
            prob.constraints[0]
            == "The number of nodes in each linked list is in the range [1, 100]."
        )
        assert prob.constraints[1] == "0 <= Node.val <= 9"
        assert prob.constraints[2] == "1 <= n <= 10^4"
        assert prob.topic_tags == ["Linked List", "Math", "Recursion"]

    async def test_fetch_problem_fallback_title_search_succeeds(
        self,
        sample_graphql_question_list_payload: dict[str, Any],
        sample_graphql_question_detail_payload: dict[str, Any],
    ) -> None:
        """Verify fallback title search resolves when initial slug lookup misses."""
        detail_calls = 0

        def handler(request: httpx.Request) -> httpx.Response:
            nonlocal detail_calls
            body = request.read().decode()
            if "problemsetQuestionList" in body:
                return httpx.Response(200, json=sample_graphql_question_list_payload)
            detail_calls += 1
            if detail_calls == 1:
                return httpx.Response(200, json={"data": {"question": None}})
            return httpx.Response(200, json=sample_graphql_question_detail_payload)

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        prob = await client.fetch_problem("Add Two Numbers")
        assert prob.slug == "add-two-numbers"
        assert detail_calls == 2

    async def test_fetch_problem_not_found(self) -> None:
        """Verify non-existent problem slug raises ProblemNotFoundError."""

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            return httpx.Response(200, json={"data": {"question": None}})

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        with pytest.raises(ProblemNotFoundError):
            await client.fetch_problem("non-existent-problem")

    async def test_fetch_problem_fallback_title_search_miss(
        self,
        sample_graphql_question_list_payload: dict[str, Any],
    ) -> None:
        """Verify fallback title search raises ProblemNotFoundError when titles differ."""

        def handler(request: httpx.Request) -> httpx.Response:
            body = request.read().decode()
            if "problemsetQuestionList" in body:
                return httpx.Response(200, json=sample_graphql_question_list_payload)
            return httpx.Response(200, json={"data": {"question": None}})

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        with pytest.raises(ProblemNotFoundError):
            await client.fetch_problem("Completely Unmatched Title")

    async def test_fetch_problem_premium_locked(self) -> None:
        """Verify premium-only problem raises PremiumProblemError."""

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            return httpx.Response(
                200,
                json={
                    "data": {
                        "question": {
                            "title": "Meeting Rooms II",
                            "isPaidOnly": True,
                        }
                    }
                },
            )

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        with pytest.raises(PremiumProblemError) as exc_info:
            await client.fetch_problem("meeting-rooms-ii")
        assert "Premium" in str(exc_info.value)


class TestNetworkResilience:
    """Tests for immutable headers, retries, timeouts, and context lifecycle."""

    pytestmark = pytest.mark.asyncio

    async def test_headers_are_immutable_frozendict(self) -> None:
        """Verify LeetCodeClient.HEADERS uses PEP 814 frozendict and rejects writes."""
        assert isinstance(LeetCodeClient.HEADERS, frozendict)
        assert LeetCodeClient.HEADERS["Content-Type"] == "application/json"
        mutable_view = cast("dict[str, str]", LeetCodeClient.HEADERS)
        with pytest.raises(TypeError):
            mutable_view["X-Custom"] = "bad"

    async def test_session_cookies_injected_when_env_set(
        self,
        monkeypatch: pytest.MonkeyPatch,
    ) -> None:
        """Verify LEETCODE_SESSION and csrftoken inject Cookie and x-csrftoken headers."""
        monkeypatch.setenv("LEETCODE_SESSION", "sess123")
        monkeypatch.setenv("csrftoken", "csrf456")
        async with LeetCodeClient() as client:
            http_client = await client._get_client()
            assert (
                http_client.headers["Cookie"]
                == "LEETCODE_SESSION=sess123; csrftoken=csrf456"
            )
            assert http_client.headers["x-csrftoken"] == "csrf456"

    async def test_retry_on_429_rate_limit_success(
        self,
        sample_graphql_question_detail_payload: dict[str, Any],
    ) -> None:
        """Verify client retries with exponential backoff on HTTP 429 and succeeds."""
        call_count = 0

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            nonlocal call_count
            call_count += 1
            if call_count < 3:
                return httpx.Response(429, headers={"Retry-After": "1"})
            return httpx.Response(200, json=sample_graphql_question_detail_payload)

        client = LeetCodeClient(
            transport=httpx.MockTransport(handler),
            max_retries=3,
            base_delay=0.1,
        )
        with patch("asyncio.sleep", new_callable=AsyncMock) as mock_sleep:
            prob = await client.fetch_problem("add-two-numbers")
            assert prob.frontend_id == 2
            assert call_count == 3
            assert mock_sleep.call_count == 2

    async def test_retry_on_429_exhausted(self) -> None:
        """Verify client raises RateLimitError when retries are exhausted."""

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            return httpx.Response(429)

        client = LeetCodeClient(
            transport=httpx.MockTransport(handler),
            max_retries=3,
            base_delay=0.01,
        )
        with (
            patch("asyncio.sleep", new_callable=AsyncMock),
            pytest.raises(RateLimitError) as exc_info,
        ):
            await client.fetch_problem("add-two-numbers")
        assert "HTTP 429" in str(exc_info.value)
        assert isinstance(exc_info.value, LeetCodeNetworkError)

    async def test_retry_on_503_exhausted(self) -> None:
        """Verify transient 5xx status exhausts retries and raises LeetCodeNetworkError."""

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            return httpx.Response(503, text="Service Unavailable")

        client = LeetCodeClient(
            transport=httpx.MockTransport(handler),
            max_retries=2,
            base_delay=0.01,
        )
        with (
            patch("asyncio.sleep", new_callable=AsyncMock),
            pytest.raises(LeetCodeNetworkError, match="HTTP 503"),
        ):
            await client.fetch_problem("add-two-numbers")

    async def test_graphql_errors_raise_network_error(self) -> None:
        """Verify GraphQL error payload without data raises LeetCodeNetworkError."""

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            return httpx.Response(200, json={"errors": [{"message": "boom"}]})

        client = LeetCodeClient(transport=httpx.MockTransport(handler))
        with pytest.raises(LeetCodeNetworkError, match="GraphQL returned errors"):
            await client.fetch_problem("add-two-numbers")

    async def test_retry_on_network_timeout(self) -> None:
        """Verify network timeouts trigger retries and raise LeetCodeNetworkError."""

        def handler(request: httpx.Request) -> httpx.Response:
            del request
            raise httpx.ReadTimeout("Connection timed out")

        client = LeetCodeClient(
            transport=httpx.MockTransport(handler),
            max_retries=2,
            base_delay=0.01,
        )
        with (
            patch("asyncio.sleep", new_callable=AsyncMock),
            pytest.raises(LeetCodeNetworkError),
        ):
            await client.fetch_problem("add-two-numbers")

    async def test_asyncio_timeout_triggers_retry_and_network_error(self) -> None:
        """Verify asyncio.timeout cancels slow requests and retries cleanly."""

        class SlowTransport(httpx.AsyncBaseTransport):
            async def handle_async_request(
                self,
                request: httpx.Request,
            ) -> httpx.Response:
                del request
                await asyncio.sleep(0.5)
                return httpx.Response(200, json={"data": {"question": None}})

        client = LeetCodeClient(
            transport=SlowTransport(),
            timeout=0.02,
            max_retries=2,
            base_delay=0.001,
        )
        with pytest.raises(LeetCodeNetworkError, match="Network error"):
            await client.fetch_problem("add-two-numbers")

    async def test_client_lifecycle_context_manager(self) -> None:
        """Verify async context manager opens and closes underlying AsyncClient cleanly."""
        async with LeetCodeClient() as client:
            await client.close()
            c = await client._get_client()
            assert not c.is_closed
        assert c.is_closed

    async def test_zero_max_retries_raises_network_error(self) -> None:
        """Verify max_retries=0 raises LeetCodeNetworkError immediately."""
        client = LeetCodeClient(max_retries=0)
        with pytest.raises(LeetCodeNetworkError, match="Exceeded maximum retry"):
            await client.fetch_problem("add-two-numbers")


class TestLiveLeetCodeAPI:
    """Live query tests against external LeetCode GraphQL API."""

    pytestmark = pytest.mark.asyncio

    @pytest.mark.live
    async def test_live_query_problem_2(self) -> None:
        """Perform real GraphQL query against LeetCode for problem #2."""
        async with LeetCodeClient(timeout=15.0) as client:
            prob = await client.fetch_problem("2")
            assert prob.frontend_id == 2
            assert prob.title == "Add Two Numbers"
            assert prob.slug == "add-two-numbers"
            assert prob.difficulty == "Medium"
            assert prob.is_paid_only is False
            assert "ListNode" in prob.cpp_snippet
            assert len(prob.example_test_cases) >= 3
            assert len(prob.constraints) >= 2
