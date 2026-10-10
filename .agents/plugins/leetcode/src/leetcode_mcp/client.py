"""Async client for querying LeetCode GraphQL API with Cloudflare resilience."""

import asyncio
import html
import logging
import os
import random
import re
from typing import TYPE_CHECKING
from typing import Any
from typing import ClassVar
from typing import Self
from typing import cast

import httpx
import markdownify

from leetcode_mcp.exceptions import LeetCodeNetworkError
from leetcode_mcp.exceptions import PremiumProblemError
from leetcode_mcp.exceptions import ProblemNotFoundError
from leetcode_mcp.exceptions import RateLimitError
from leetcode_mcp.models import ProblemDetails


if TYPE_CHECKING:
    from types import TracebackType


logger = logging.getLogger(__name__)


class LeetCodeClient:
    """Async client for querying LeetCode GraphQL API with connection pooling and retries.

    Attributes:
        GRAPHQL_URL: The official LeetCode GraphQL endpoint URL.
        HEADERS: Default browser headers to pass Cloudflare bot-management checks.
        QUESTION_LIST_QUERY: GraphQL query for resolving problem IDs or keywords to title slugs.
        QUESTION_DETAIL_QUERY: GraphQL query for extracting comprehensive problem details.
    """

    GRAPHQL_URL: ClassVar[str] = 'https://leetcode.com/graphql'
    HEADERS: ClassVar[dict[str, str]] = {
        'Content-Type': 'application/json',
        'User-Agent': (
            'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) '
            'AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
        ),
        'Accept': '*/*',
        'Accept-Language': 'en-US,en;q=0.9',
        'Origin': 'https://leetcode.com',
        'Referer': 'https://leetcode.com',
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
            headers = dict(self.HEADERS)
            leetcode_session = os.environ.get('LEETCODE_SESSION')
            csrftoken = os.environ.get('csrftoken')
            if leetcode_session and csrftoken:
                headers['Cookie'] = f'LEETCODE_SESSION={leetcode_session}; csrftoken={csrftoken}'
                headers['x-csrftoken'] = csrftoken

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

    async def _post_graphql(self, query: str, variables: dict[str, Any]) -> dict[str, Any]:
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
                resp = await client.post(
                    self.GRAPHQL_URL,
                    json={'query': query, 'variables': variables},
                )
                # Handle rate limiting and transient upstream server errors
                if resp.status_code in (429, 499, 500, 502, 503, 504):
                    if attempt == self.max_retries - 1:
                        if resp.status_code == 429:
                            raise RateLimitError(
                                f'HTTP 429 Rate Limit from LeetCode after {self.max_retries} attempts.'
                            )
                        raise LeetCodeNetworkError(
                            f'HTTP {resp.status_code} from LeetCode after {self.max_retries} '
                            f'attempts: {resp.text[:200]}'
                        )
                    retry_after = resp.headers.get('Retry-After')
                    if retry_after and retry_after.isdigit():
                        delay = float(retry_after)
                    else:
                        jitter = random.uniform(0, 0.1 * self.base_delay)
                        delay = self.base_delay * (2**attempt) + jitter

                    logger.warning(
                        'LeetCode request received status %d. Retrying in %.2fs (attempt %d/%d)...',
                        resp.status_code,
                        delay,
                        attempt + 1,
                        self.max_retries,
                    )
                    await asyncio.sleep(delay)
                    continue

                resp.raise_for_status()
                data = cast('dict[str, Any]', resp.json())
                if 'errors' in data and not data.get('data'):
                    err_msg = str(data.get('errors'))
                    raise LeetCodeNetworkError(f'GraphQL returned errors: {err_msg}')
                return data

            except (httpx.TimeoutException, httpx.NetworkError) as err:
                if attempt == self.max_retries - 1:
                    raise LeetCodeNetworkError(
                        f'Network error communicating with LeetCode after {self.max_retries} attempts: {err}'
                    ) from err
                jitter = random.uniform(0, 0.1 * self.base_delay)
                delay = self.base_delay * (2**attempt) + jitter
                logger.warning(
                    'Network error %s. Retrying in %.2fs (attempt %d/%d)...',
                    err,
                    delay,
                    attempt + 1,
                    self.max_retries,
                )
                await asyncio.sleep(delay)

        raise LeetCodeNetworkError('Exceeded maximum retry attempts against LeetCode GraphQL.')

    def normalize_input(self, query: str | int) -> tuple[str | None, str | None]:
        """Extract problem slug or numeric ID from raw query string or integer.

        Args:
            query: Raw user input such as 2, '2', '#2', 'LeetCode #2', full URL, or title.

        Returns:
            A tuple `(slug, num_id)` where exactly one is non-None.
            - `(slug, None)` if a slug, title, or URL was identified.
            - `(None, num_id)` if a numeric problem ID was identified.
            - `(None, None)` if the query was empty.
        """
        raw = str(query).strip()
        if not raw:
            return None, None

        # 1. URL pattern: matches leetcode.com/problems/<slug>
        url_match = re.search(r'leetcode\.com/problems/([a-z0-9\-]+)', raw, re.IGNORECASE)
        if url_match:
            return url_match.group(1).lower(), None

        # 2. Numeric pattern: optional prefix (e.g. leetcode, problem, #), then integer digits
        num_match = re.match(
            r'^(?:(?:leetcode\s*(?:problem)?\s*#?)|#|problem\s*#?|\s*)*(\d+)$',
            raw,
            re.IGNORECASE,
        )
        if num_match:
            return None, num_match.group(1)

        # 3. Slug or Title: convert spaces to hyphens, strip non-alphanumeric/hyphen
        cleaned = raw.lower()
        cleaned = re.sub(r'[^a-z0-9\-\s]', '', cleaned)
        cleaned = re.sub(r'[\s_]+', '-', cleaned).strip('-')
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
            raise ProblemNotFoundError(f'Invalid problem query: {query!r}')

        if slug and not num_id:
            return slug

        if num_id is None:  # pragma: no cover - normalize_input guarantees a slug or an id
            raise ProblemNotFoundError(f'Invalid problem query: {query!r}')
        data = await self._post_graphql(
            self.QUESTION_LIST_QUERY,
            {'categorySlug': '', 'skip': 0, 'limit': 100, 'filters': {'searchKeywords': num_id}},
        )
        questions = data.get('data', {}).get('problemsetQuestionList', {}).get('questions', [])
        for q in questions:
            if str(q.get('frontendQuestionId')) == str(num_id):
                return str(q['titleSlug'])

        raise ProblemNotFoundError(f'LeetCode problem #{num_id} could not be found.')

    def _extract_constraints(self, html_content: str) -> list[str]:
        """Parse constraints from problem HTML description.

        Preserves mathematical exponents (<sup>4</sup> -> ^4) and subscripts,
        decodes HTML entities, and strips formatting tags.

        Args:
            html_content: Raw HTML problem description.

        Returns:
            List of clean constraint strings.
        """
        constraints: list[str] = []
        m = re.search(
            r'<strong>\s*Constraints:?\s*</strong>.*?(<ul>.*?</ul>)',
            html_content,
            re.DOTALL | re.IGNORECASE,
        )
        if not m:
            return constraints

        for li in re.findall(r'<li>(.*?)</li>', m.group(1), re.DOTALL):
            s = re.sub(r'<sup>(.*?)</sup>', r'^\1', li, flags=re.IGNORECASE)
            s = re.sub(r'<sub>(.*?)</sub>', r'_\1', s, flags=re.IGNORECASE)
            s = re.sub(r'<[^>]+>', '', s)
            s = html.unescape(s)
            s = s.replace('\xa0', ' ').strip()
            if s:
                constraints.append(s)
        return constraints

    def _clean_markdown(self, html_content: str) -> str:
        """Convert HTML problem description to clean Markdown.

        Args:
            html_content: Raw HTML string.

        Returns:
            Formatted Markdown text with standardized spacing.
        """
        md = markdownify.markdownify(html_content, heading_style='ATX')
        md = md.replace('\xa0', ' ')
        md = re.sub(r'\n{3,}', '\n\n', md)
        return md.strip()

    async def fetch_problem(self, query: str | int) -> ProblemDetails:
        """Fetch complete problem details and starter assets for a given query.

        Args:
            query: Problem identifier (number, slug, URL, or title).

        Returns:
            Validated ProblemDetails Pydantic model.

        Raises:
            ProblemNotFoundError: If the problem does not exist on LeetCode.
            PremiumProblemError: If the problem requires a LeetCode Premium subscription.
            LeetCodeNetworkError: If network connectivity or rate limit retries fail.
        """
        slug = await self.resolve_slug(query)
        data = await self._post_graphql(self.QUESTION_DETAIL_QUERY, {'titleSlug': slug})
        q = data.get('data', {}).get('question')

        if not q:
            # Fallback search if query was a title with slight naming variations
            _, num_id = self.normalize_input(query)
            if not num_id:
                raw_kw = str(query).strip()
                search_data = await self._post_graphql(
                    self.QUESTION_LIST_QUERY,
                    {
                        'categorySlug': '',
                        'skip': 0,
                        'limit': 50,
                        'filters': {'searchKeywords': raw_kw},
                    },
                )
                candidates = search_data.get('data', {}).get('problemsetQuestionList', {}).get('questions', [])
                for candidate in candidates:
                    if candidate.get('title', '').lower() == raw_kw.lower():
                        slug = str(candidate['titleSlug'])
                        retry_data = await self._post_graphql(self.QUESTION_DETAIL_QUERY, {'titleSlug': slug})
                        q = retry_data.get('data', {}).get('question')
                        break

            if not q:
                raise ProblemNotFoundError(f'Problem {query!r} (slug: {slug!r}) not found on LeetCode.')

        if q.get('isPaidOnly') and not q.get('content'):
            raise PremiumProblemError(
                f"Problem '{q.get('title')}' (#{q.get('questionFrontendId')}) is a LeetCode "
                'Premium problem. Unauthenticated requests cannot retrieve its description '
                'or starter code.'
            )

        html_content = str(q.get('content') or '')
        markdown_desc = self._clean_markdown(html_content)

        snippets_by_lang = {
            str(snippet.get('langSlug')): str(snippet.get('code', '')) for snippet in q.get('codeSnippets') or []
        }
        cpp_code = snippets_by_lang.get('cpp', '')
        python_code = snippets_by_lang.get('python3', '')

        constraints = self._extract_constraints(html_content)
        topic_tags = [str(t['name']) for t in (q.get('topicTags') or []) if 'name' in t]

        return ProblemDetails(
            frontend_id=int(q['questionFrontendId']),
            question_id=int(q['questionId']),
            title=str(q['title']),
            slug=str(q['titleSlug']),
            difficulty=str(q['difficulty']),
            is_paid_only=bool(q['isPaidOnly']),
            description_markdown=markdown_desc,
            cpp_snippet=cpp_code,
            python_snippet=python_code,
            sample_test_case=str(q.get('sampleTestCase') or ''),
            example_test_cases=[str(tc) for tc in (q.get('exampleTestcaseList') or [])],
            constraints=constraints,
            topic_tags=topic_tags,
            hints=[str(h) for h in (q.get('hints') or [])],
        )
