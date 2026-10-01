"""Pydantic data models for LeetCode MCP server."""

from pydantic import BaseModel, ConfigDict, Field


class ProblemMetadata(BaseModel):
    """Metadata and descriptive fields for a LeetCode problem.

    Attributes:
        frontend_id: The problem number as seen on LeetCode (e.g. 2).
        question_id: Internal LeetCode question ID.
        title: Official problem title (e.g. 'Add Two Numbers').
        slug: URL title slug (e.g. 'add-two-numbers').
        difficulty: Problem difficulty: Easy, Medium, or Hard.
        is_paid_only: Whether the problem requires LeetCode Premium.
        topic_tags: Associated topic tags.
        hints: Official problem hints if available.
    """

    model_config = ConfigDict(frozen=True)

    frontend_id: int = Field(description="The problem number as seen on LeetCode (e.g. 2)")
    question_id: int = Field(description="Internal LeetCode question ID")
    title: str = Field(description="Official problem title (e.g. 'Add Two Numbers')")
    slug: str = Field(description="URL title slug (e.g. 'add-two-numbers')")
    difficulty: str = Field(description="Problem difficulty: Easy, Medium, or Hard")
    is_paid_only: bool = Field(description="Whether the problem requires LeetCode Premium")
    topic_tags: list[str] = Field(default_factory=list, description="Associated topic tags")
    hints: list[str] = Field(
        default_factory=list, description="Official problem hints if available"
    )

    @property
    def slug_snake(self) -> str:
        """Return slug converted to snake_case for C++ file and target naming."""
        return self.slug.replace("-", "_")

    @property
    def header_filename(self) -> str:
        """Return canonical C++ header filename (e.g. 'add_two_numbers.hpp')."""
        return f"{self.slug_snake}.hpp"

    @property
    def source_filename(self) -> str:
        """Return canonical C++ source filename (e.g. 'add_two_numbers.cpp')."""
        return f"{self.slug_snake}.cpp"

    @property
    def test_filename(self) -> str:
        """Return canonical Google Test filename (e.g. 'add_two_numbers_test.cpp')."""
        return f"{self.slug_snake}_test.cpp"


class ProblemDetails(ProblemMetadata):
    """Full problem data including Markdown description, C++ snippets, and test cases.

    Attributes:
        description_markdown: Clean Markdown converted from HTML problem description.
        cpp_snippet: Official LeetCode C++ starter code snippet.
        sample_test_case: Raw default sample test case string.
        example_test_cases: Parsed example input test cases.
        constraints: Extracted problem constraints preserving mathematical formatting.
    """

    description_markdown: str = Field(
        description="Clean Markdown converted from HTML problem description"
    )
    cpp_snippet: str = Field(description="Official LeetCode C++ starter code snippet")
    sample_test_case: str = Field(description="Raw default sample test case string")
    example_test_cases: list[str] = Field(
        default_factory=list, description="Parsed example input test cases"
    )
    constraints: list[str] = Field(
        default_factory=list, description="Extracted problem constraints"
    )
