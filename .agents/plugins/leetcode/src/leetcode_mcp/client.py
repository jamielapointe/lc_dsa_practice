"""Async client for querying LeetCode GraphQL API with Cloudflare resilience."""

import asyncio
import html
import logging
import os
import random
import re
import sys
from typing import TYPE_CHECKING, Any, ClassVar, Final, Self, cast, override

import httpx
import markdownify
import structlog

from leetcode_mcp.exceptions import (
    LeetCodeNetworkError,
    PremiumProblemError,
    ProblemNotFoundError,
    RateLimitError,
)
from leetcode_mcp.models import ProblemDetails


if TYPE_CHECKING:
    from types import TracebackType

    from structlog.typing import FilteringBoundLogger


class _StderrPrintLoggerFactory(structlog.PrintLoggerFactory):
    """PrintLoggerFactory that binds loggers dynamically to active ``sys.stderr``."""

    def __init__(self) -> None:
        super().__init__(file=sys.stderr)

    @override
    def __call__(self, *args: object) -> structlog.PrintLogger:
        """Create a ``PrintLogger`` targeting the active ``sys.stderr``.

        Returns:
            A ``structlog.PrintLogger`` bound strictly to ``sys.stderr``.
        """
        del args
        return structlog.PrintLogger(file=sys.stderr)


def configure_logging(level: int = logging.INFO) -> None:
    """Configure structlog to emit structured JSON logs strictly to ``sys.stderr``.

    MCP stdio transport reserves ``sys.stdout`` exclusively for JSON-RPC frames,
    so every log event must route to ``sys.stderr``.

    Args:
        level: Minimum log level threshold passed to ``make_filtering_bound_logger``.
    """
    structlog.configure(
        processors=[
            structlog.contextvars.merge_contextvars,
            structlog.processors.add_log_level,
            structlog.processors.StackInfoRenderer(),
            structlog.processors.format_exc_info,
            structlog.processors.TimeStamper(fmt="iso", utc=True),
            structlog.processors.JSONRenderer(),
        ],
        wrapper_class=structlog.make_filtering_bound_logger(level),
        logger_factory=_StderrPrintLoggerFactory(),
        cache_logger_on_first_use=False,
    )


if not structlog.is_configured():
    configure_logging()

logger: FilteringBoundLogger = structlog.get_logger("leetcode_mcp.client")

_HTTP_TOO_MANY_REQUESTS: Final[int] = 429
_RETRYABLE_STATUS_CODES: Final[frozenset[int]] = frozenset({
    429,
    499,
    500,
    502,
    503,
    504,
})
_URL_SLUG_RE: Final[re.Pattern[str]] = re.compile(
    r"leetcode\.com/problems/([a-z0-9\-]+)",
    re.IGNORECASE | re.ASCII,
)
_NUMERIC_QUERY_RE: Final[re.Pattern[str]] = re.compile(
    r"^(?:leetcode\s*)?(?:problem\s*)?#?\s*([0-9]+)$",
    re.IGNORECASE | re.ASCII,
)
_NON_SLUG_CHARS_RE: Final[re.Pattern[str]] = re.compile(r"[^a-z0-9_\-\s]")
_WHITESPACE_OR_UNDERSCORE_RE: Final[re.Pattern[str]] = re.compile(r"[\s_]+")
_CONSTRAINTS_BLOCK_RE: Final[re.Pattern[str]] = re.compile(
    r"<strong>\s*Constraints:?\s*</strong>.*?(<ul>.*?</ul>)",
    re.DOTALL | re.IGNORECASE,
)
_LIST_ITEM_RE: Final[re.Pattern[str]] = re.compile(r"<li>(.*?)</li>", re.DOTALL)
_SUP_TAG_RE: Final[re.Pattern[str]] = re.compile(r"<sup>(.*?)</sup>", re.IGNORECASE)
_SUB_TAG_RE: Final[re.Pattern[str]] = re.compile(r"<sub>(.*?)</sub>", re.IGNORECASE)
_HTML_TAG_RE: Final[re.Pattern[str]] = re.compile(r"<[^>]+>")
_EXCESS_NEWLINES_RE: Final[re.Pattern[str]] = re.compile(r"\n{3,}")


class LeetCodeClient:
    """Async client for querying LeetCode GraphQL API with connection pooling and retries.

    Attributes:
        GRAPHQL_URL: The official LeetCode GraphQL endpoint URL.
        HEADERS: Immutable browser headers (PEP 814 ``frozendict``) to pass Cloudflare checks.
        QUESTION_LIST_QUERY: GraphQL query for resolving problem IDs or keywords to title slugs.
        QUESTION_DETAIL_QUERY: GraphQL query for extracting comprehensive problem details.
    """

    GRAPHQL_URL: ClassVar[str] = "https://leetcode.com/graphql"
    HEADERS: ClassVar[frozendict[str, str]] = frozendict[str, str]() | {
        "Content-Type": "application/json",
        "User-Agent": (
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
        ),
        "Accept": "*/*",
        "Accept-Language": "en-US,en;q=0.9",
        "Origin": "https://leetcode.com",
        "Referer": "https://leetcode.com",
    }

    QUESTION_LIST_QUERY: ClassVar[str] = """
    query problemsetQuestionList(
      $categorySlug: String,
      $limit: Int,
      $skip: Int,
      $filters: QuestionListFilterInput
    ) {
      problemsetQuestionList: questionList(
        categorySlug: $categorySlug
        limit: $limit
        skip: $skip
        filters: $filters
      ) {
        total: totalNum
        questions: data {
          frontendQuestionId: questionFrontendId
          title
          titleSlug
        }
      }
    }
    """

    QUESTION_DETAIL_QUERY: ClassVar[str] = """
    query getQuestionDetail($titleSlug: String!) {
      question(titleSlug: $titleSlug) {
        questionId
        questionFrontendId
        title
        titleSlug
        difficulty
        content
        isPaidOnly
        codeSnippets {
          lang
          langSlug
          code
        }
        sampleTestCase
        exampleTestcaseList
        topicTags {
          name
          slug
        }
        hints
      }
    }
    """

    def __init__(
        self,
        timeout: float = 10.0,
        max_retries: int = 3,
        base_delay: float = 1.0,
        transport: httpx.AsyncBaseTransport | None = None,
    ) -> None:
        """Initialize LeetCodeClient with timeout, retry count, and backoff configuration.

        Args:
            timeout: Maximum request timeout in seconds.
            max_retries: Maximum number of retry attempts on network error or HTTP 429.
            base_delay: Initial backoff delay in seconds for exponential backoff.
            transport: Optional custom HTTPX transport (e.g. MockTransport for testing).
        """
        self.timeout = timeout
        self.max_retries = max_retries
        self.base_delay = base_delay
        self.transport = transport
        self._client: httpx.AsyncClient | None = None

    async def _get_client(self) -> httpx.AsyncClient:
        """Retrieve or initialize persistent httpx.AsyncClient connection pool.

        Returns:
            The shared, lazily created HTTP client.
        """
        if self._client is None or self._client.is_closed:
            headers: frozendict[str, str] = self.HEADERS
            leetcode_session = os.environ.get("LEETCODE_SESSION")
            csrftoken = os.environ.get(
                "csrftoken"  # ruff: ignore[uncapitalized-environment-variables] - LeetCode cookie env var.
            )
            if leetcode_session and csrftoken:
                headers |= {
                    "Cookie": f"LEETCODE_SESSION={leetcode_session}; csrftoken={csrftoken}",
                    "x-csrftoken": csrftoken,
                }

            self._client = httpx.AsyncClient(
                transport=self.transport,
                headers=headers,
                timeout=httpx.Timeout(self.timeout, connect=5.0),
                limits=httpx.Limits(max_keepalive_connections=5, max_connections=10),
            )
        return self._client

    async def close(self) -> None:
        """Close underlying HTTP client connection pool."""
        if self._client and not self._client.is_closed:
            await self._client.aclose()

    async def __aenter__(self) -> Self:
        """Async context manager entry.

        Returns:
            This client.
        """
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: TracebackType | None,
    ) -> None:
        """Async context manager exit, ensuring connection pool closure."""
        await self.close()

    def _compute_backoff_delay(self, attempt: int) -> float:
        """Compute exponential backoff delay with uniform jitter.

        Args:
            attempt: Zero-based retry attempt index.

        Returns:
            Delay duration in seconds.
        """
        jitter = random.uniform(0, 0.1 * self.base_delay)
        return self.base_delay * (2**attempt) + jitter

    def _compute_retry_delay(self, resp: httpx.Response, attempt: int) -> float:
        """Compute retry delay honoring an integer Retry-After header when present.

        Args:
            resp: HTTP response received from the failed attempt.
            attempt: Zero-based retry attempt index.

        Returns:
            Delay duration in seconds before the next attempt.
        """
        retry_after = resp.headers.get("Retry-After")
        if retry_after and retry_after.isdigit():
            return float(retry_after)
        return self._compute_backoff_delay(attempt)

    async def _send_graphql_attempt(
        self,
        client: httpx.AsyncClient,
        query: str,
        variables: dict[str, Any],
    ) -> httpx.Response:
        """Send one GraphQL POST request bounded by ``asyncio.timeout``.

        Args:
            client: Active HTTPX async client.
            query: GraphQL query string.
            variables: GraphQL query variables mapping.

        Returns:
            Raw HTTP response returned by LeetCode.
        """
        async with asyncio.timeout(self.timeout):
            return await client.post(
                self.GRAPHQL_URL,
                json={"query": query, "variables": variables},
            )

    @staticmethod
    def _parse_graphql_response(resp: httpx.Response) -> dict[str, Any]:
        """Validate HTTP status and decode GraphQL JSON payload.

        Args:
            resp: HTTP response returned from LeetCode GraphQL.

        Returns:
            Decoded JSON dictionary.

        Raises:
            LeetCodeNetworkError: If the GraphQL payload reports top-level errors without data.
        """
        resp.raise_for_status()
        data = cast("dict[str, Any]", resp.json())
        if "errors" in data and not data.get("data"):
            err_msg = str(data.get("errors"))
            raise LeetCodeNetworkError(f"GraphQL returned errors: {err_msg}")
        return data

    async def _post_graphql(
        self,
        query: str,
        variables: dict[str, Any],
    ) -> dict[str, Any]:
        """Execute GraphQL POST request with exponential backoff retry logic.

        Args:
            query: GraphQL query string.
            variables: Query variables dictionary.

        Returns:
            Parsed JSON dictionary returned by LeetCode GraphQL.

        Raises:
            RateLimitError: If HTTP 429 persists after exhausting retries.
            LeetCodeNetworkError: If retries are exhausted or server returns persistent errors.
        """
        client = await self._get_client()
        for attempt in range(self.max_retries):
            try:
                resp = await self._send_graphql_attempt(client, query, variables)
            except (TimeoutError, httpx.TimeoutException, httpx.NetworkError) as err:
                if attempt == self.max_retries - 1:
                    raise LeetCodeNetworkError(
                        f"Network error communicating with LeetCode after "
                        f"{self.max_retries} attempts: {err}"
                    ) from err
                delay = self._compute_backoff_delay(attempt)
                logger.warning(
                    "leetcode_network_retry",
                    error=str(err),
                    delay_s=round(delay, 2),
                    attempt=attempt + 1,
                    max_retries=self.max_retries,
                )
                await asyncio.sleep(delay)
                continue

            if resp.status_code in _RETRYABLE_STATUS_CODES:
                if attempt == self.max_retries - 1:
                    if resp.status_code == _HTTP_TOO_MANY_REQUESTS:
                        raise RateLimitError(
                            f"HTTP 429 Rate Limit from LeetCode after "
                            f"{self.max_retries} attempts."
                        )
                    raise LeetCodeNetworkError(
                        f"HTTP {resp.status_code} from LeetCode after "
                        f"{self.max_retries} attempts: {resp.text[:200]}"
                    )
                delay = self._compute_retry_delay(resp, attempt)
                logger.warning(
                    "leetcode_request_retry",
                    status_code=resp.status_code,
                    delay_s=round(delay, 2),
                    attempt=attempt + 1,
                    max_retries=self.max_retries,
                )
                await asyncio.sleep(delay)
                continue

            return self._parse_graphql_response(resp)

        raise LeetCodeNetworkError(
            "Exceeded maximum retry attempts against LeetCode GraphQL."
        )

    @staticmethod
    def normalize_input(query: str | int) -> tuple[str | None, str | None]:
        """Extract problem slug or numeric ID from raw query string or integer.

        Args:
            query: Raw user input such as 2, '2', '#2', 'LeetCode #2', full URL, or title.

        Returns:
            A tuple ``(slug, num_id)`` where at most one element is non-None:
            - ``(slug, None)`` if a slug, title, or URL was identified.
            - ``(None, num_id)`` if a numeric problem ID was identified.
            - ``(None, None)`` if the query was empty or contained no slug tokens.
        """
        raw = str(query).strip()
        if not raw:
            return None, None

        url_match = _URL_SLUG_RE.search(raw)
        if url_match:
            slug = url_match.group(1).lower().strip("-")
            return (slug, None) if slug else (None, None)

        num_match = _NUMERIC_QUERY_RE.match(raw)
        if num_match:
            return None, num_match.group(1)

        cleaned = raw.lower()
        cleaned = _NON_SLUG_CHARS_RE.sub("", cleaned)
        cleaned = _WHITESPACE_OR_UNDERSCORE_RE.sub("-", cleaned).strip("-")
        return (cleaned, None) if cleaned else (None, None)

    async def resolve_slug(self, query: str | int) -> str:
        """Resolve any problem query into a canonical LeetCode title slug.

        Args:
            query: Problem identifier (number, slug, URL, or title).

        Returns:
            The canonical title slug string (e.g. 'add-two-numbers').

        Raises:
            ProblemNotFoundError: If numeric ID does not exist or input is invalid.
        """
        slug, num_id = self.normalize_input(query)
        if not slug and not num_id:
            raise ProblemNotFoundError(f"Invalid problem query: {query!r}")

        if slug and not num_id:
            return slug

        if (
            num_id is None
        ):  # pragma: no cover - normalize_input guarantees slug or num_id
            raise ProblemNotFoundError(f"Invalid problem query: {query!r}")

        data = await self._post_graphql(
            self.QUESTION_LIST_QUERY,
            {
                "categorySlug": "",
                "skip": 0,
                "limit": 100,
                "filters": {"searchKeywords": num_id},
            },
        )
        questions: list[dict[str, Any]] = (
            data.get("data", {}).get("problemsetQuestionList", {}).get("questions", [])
        )
        for q in questions:
            if str(q.get("frontendQuestionId")) == str(num_id):
                return str(q["titleSlug"])

        raise ProblemNotFoundError(f"LeetCode problem #{num_id} could not be found.")

    @staticmethod
    def _extract_constraints(html_content: str) -> list[str]:
        """Parse constraints from problem HTML description.

        Preserves mathematical exponents (``<sup>4</sup>`` -> ``^4``) and subscripts,
        decodes HTML entities, and strips formatting tags.

        Args:
            html_content: Raw HTML problem description.

        Returns:
            List of clean constraint strings.
        """
        constraints: list[str] = []
        match = _CONSTRAINTS_BLOCK_RE.search(html_content)
        if not match:
            return constraints

        for li in _LIST_ITEM_RE.findall(match.group(1)):
            s = _SUP_TAG_RE.sub(r"^\1", li)
            s = _SUB_TAG_RE.sub(r"_\1", s)
            s = _HTML_TAG_RE.sub("", s)
            s = html.unescape(s).replace("\xa0", " ").strip()
            if s:
                constraints.append(s)
        return constraints

    @staticmethod
    def _clean_markdown(html_content: str) -> str:
        """Convert HTML problem description to clean Markdown.

        Args:
            html_content: Raw HTML string.

        Returns:
            Formatted Markdown text with standardized spacing.
        """
        md = markdownify.markdownify(html_content, heading_style="ATX")
        md = md.replace("\xa0", " ")
        md = _EXCESS_NEWLINES_RE.sub("\n\n", md)
        return md.strip()

    async def _fallback_search_question(
        self, query: str | int
    ) -> dict[str, Any] | None:
        """Search question list by exact title when direct slug lookup returns empty.

        Args:
            query: Original user problem query string or integer.

        Returns:
            Raw GraphQL question dictionary if an exact title match is found, else ``None``.
        """
        _, num_id = self.normalize_input(query)
        if num_id:
            return None

        raw_kw = str(query).strip()
        search_data = await self._post_graphql(
            self.QUESTION_LIST_QUERY,
            {
                "categorySlug": "",
                "skip": 0,
                "limit": 50,
                "filters": {"searchKeywords": raw_kw},
            },
        )
        candidates: list[dict[str, Any]] = (
            search_data
            .get("data", {})
            .get("problemsetQuestionList", {})
            .get("questions", [])
        )
        for candidate in candidates:
            if str(candidate.get("title", "")).lower() == raw_kw.lower():
                resolved_slug = str(candidate["titleSlug"])
                retry_data = await self._post_graphql(
                    self.QUESTION_DETAIL_QUERY,
                    {"titleSlug": resolved_slug},
                )
                return cast(
                    "dict[str, Any] | None",
                    retry_data.get("data", {}).get("question"),
                )
        return None

    def _build_problem_details(self, question: dict[str, Any]) -> ProblemDetails:
        """Build a validated ProblemDetails instance from raw GraphQL question data.

        Args:
            question: Raw GraphQL ``question`` dictionary from LeetCode.

        Returns:
            Populated and validated ``ProblemDetails`` model.
        """
        html_content = str(question.get("content") or "")
        snippets_by_lang = {
            str(snippet.get("langSlug")): str(snippet.get("code", ""))
            for snippet in question.get("codeSnippets") or []
        }
        cpp_code = snippets_by_lang.get("cpp", "")
        python_code = snippets_by_lang.get("python3", "")
        rust_code = snippets_by_lang.get("rust", "")
        topic_tags = [
            str(tag["name"])
            for tag in (question.get("topicTags") or [])
            if "name" in tag
        ]
        return ProblemDetails(
            frontend_id=int(question["questionFrontendId"]),
            question_id=int(question["questionId"]),
            title=str(question["title"]),
            slug=str(question["titleSlug"]),
            difficulty=str(question["difficulty"]),
            is_paid_only=bool(question["isPaidOnly"]),
            description_markdown=self._clean_markdown(html_content),
            cpp_snippet=cpp_code,
            python_snippet=python_code,
            rust_snippet=rust_code,
            sample_test_case=str(question.get("sampleTestCase") or ""),
            example_test_cases=[
                str(tc) for tc in (question.get("exampleTestcaseList") or [])
            ],
            constraints=self._extract_constraints(html_content),
            topic_tags=topic_tags,
            hints=[str(hint) for hint in (question.get("hints") or [])],
        )

    async def fetch_problem(self, query: str | int) -> ProblemDetails:
        """Fetch complete problem details and starter assets for a given query.

        Propagates network/rate-limit errors raised during GraphQL requests.

        Args:
            query: Problem identifier (number, slug, URL, or title).

        Returns:
            Validated ProblemDetails Pydantic model.

        Raises:
            ProblemNotFoundError: If the problem does not exist on LeetCode.
            PremiumProblemError: If the problem requires a LeetCode Premium subscription.
        """
        slug = await self.resolve_slug(query)
        data = await self._post_graphql(self.QUESTION_DETAIL_QUERY, {"titleSlug": slug})
        question = cast("dict[str, Any] | None", data.get("data", {}).get("question"))

        if not question:
            question = await self._fallback_search_question(query)

        if not question:
            raise ProblemNotFoundError(
                f"Problem {query!r} (slug: {slug!r}) not found on LeetCode."
            )

        if question.get("isPaidOnly") and not question.get("content"):
            raise PremiumProblemError(
                f"Problem '{question.get('title')}' (#{question.get('questionFrontendId')}) "
                "is a LeetCode Premium problem. Unauthenticated requests cannot "
                "retrieve its description or starter code."
            )

        return self._build_problem_details(question)
