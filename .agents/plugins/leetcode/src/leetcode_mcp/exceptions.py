"""Custom exceptions for LeetCode MCP operations."""


class LeetCodeError(Exception):
    """Base exception for all LeetCode MCP operations."""


class ProblemNotFoundError(LeetCodeError):
    """Raised when a requested problem ID, slug, or title cannot be found."""


class PremiumProblemError(LeetCodeError):
    """Raised when a problem requires LeetCode Premium subscription to view."""


class LeetCodeNetworkError(LeetCodeError):
    """Raised when network connectivity fails, timeouts occur, or rate limits persist."""


class RateLimitError(LeetCodeNetworkError):
    """Raised when LeetCode rate limit (HTTP 429) is encountered and retries fail."""
