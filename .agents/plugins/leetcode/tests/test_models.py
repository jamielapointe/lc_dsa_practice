"""Unit and property-based tests for LeetCode MCP Pydantic models."""

from typing import Any

import pytest
from hypothesis import given
from hypothesis import strategies as st
from pydantic import ValidationError

from leetcode_mcp.models import ProblemDetails, ProblemMetadata


_SLUG_STRATEGY = st.from_regex(r"[a-z0-9]+(?:-[a-z0-9]+)*", fullmatch=True)
_DIFFICULTY_STRATEGY = st.sampled_from(["Easy", "Medium", "Hard"])
_POSITIVE_ID_STRATEGY = st.integers(min_value=1, max_value=100_000)

_METADATA_STRATEGY = st.builds(
    ProblemMetadata,
    frontend_id=_POSITIVE_ID_STRATEGY,
    question_id=_POSITIVE_ID_STRATEGY,
    title=st.text(min_size=1, max_size=80),
    slug=_SLUG_STRATEGY,
    difficulty=_DIFFICULTY_STRATEGY,
    is_paid_only=st.booleans(),
    topic_tags=st.lists(st.text(max_size=24), max_size=5),
    hints=st.lists(st.text(max_size=64), max_size=4),
)

_DETAILS_STRATEGY = st.builds(
    ProblemDetails,
    frontend_id=_POSITIVE_ID_STRATEGY,
    question_id=_POSITIVE_ID_STRATEGY,
    title=st.text(min_size=1, max_size=80),
    slug=_SLUG_STRATEGY,
    difficulty=_DIFFICULTY_STRATEGY,
    is_paid_only=st.booleans(),
    topic_tags=st.lists(st.text(max_size=24), max_size=5),
    hints=st.lists(st.text(max_size=64), max_size=4),
    description_markdown=st.text(max_size=200),
    cpp_snippet=st.text(max_size=120),
    python_snippet=st.text(max_size=120),
    rust_snippet=st.text(max_size=120),
    sample_test_case=st.text(max_size=80),
    example_test_cases=st.lists(st.text(max_size=40), max_size=4),
    constraints=st.lists(st.text(max_size=60), max_size=4),
)


class TestProblemMetadata:
    """Unit tests for ProblemMetadata schema, validation, and immutability."""

    def test_valid_instantiation(
        self,
        sample_problem_metadata_dict: dict[str, Any],
    ) -> None:
        """Verify model accepts and stores valid metadata fields."""
        meta = ProblemMetadata.model_validate(sample_problem_metadata_dict)
        assert meta.frontend_id == 2
        assert meta.question_id == 2
        assert meta.title == "Add Two Numbers"
        assert meta.slug == "add-two-numbers"
        assert meta.difficulty == "Medium"
        assert meta.is_paid_only is False
        assert meta.topic_tags == ["Linked List", "Math", "Recursion"]
        assert meta.hints == []

    def test_default_field_values(self) -> None:
        """Verify topic_tags and hints default to empty lists when omitted."""
        meta = ProblemMetadata(
            frontend_id=1,
            question_id=1,
            title="Two Sum",
            slug="two-sum",
            difficulty="Easy",
            is_paid_only=False,
        )
        assert meta.topic_tags == []
        assert meta.hints == []

    @pytest.mark.parametrize(
        "field",
        ["frontend_id", "question_id", "title", "slug", "difficulty", "is_paid_only"],
    )
    def test_missing_required_fields_raises_validation_error(
        self,
        sample_problem_metadata_dict: dict[str, Any],
        field: str,
    ) -> None:
        """Verify omitting any required field raises ValidationError."""
        data = sample_problem_metadata_dict.copy()
        del data[field]
        with pytest.raises(ValidationError) as exc_info:
            ProblemMetadata.model_validate(data)
        assert any(e["loc"][0] == field for e in exc_info.value.errors())

    def test_type_coercion(self) -> None:
        """Verify valid type coercions succeed."""
        meta = ProblemMetadata(
            frontend_id=42,
            question_id=42,
            title="Trapping Rain Water",
            slug="trapping-rain-water",
            difficulty="Hard",
            is_paid_only=False,
        )
        assert meta.frontend_id == 42
        assert meta.is_paid_only is False

    def test_invalid_type_raises_validation_error(self) -> None:
        """Verify invalid non-convertible types raise ValidationError."""
        kwargs: dict[str, Any] = {
            "frontend_id": "not_an_int",
            "question_id": 1,
            "title": "T",
            "slug": "t",
            "difficulty": "Easy",
            "is_paid_only": False,
        }
        with pytest.raises(ValidationError):
            ProblemMetadata(**kwargs)

    def test_frozen_immutability(
        self,
        sample_problem_metadata_dict: dict[str, Any],
    ) -> None:
        """Verify models configured with frozen=True reject attribute mutation."""
        meta = ProblemMetadata.model_validate(sample_problem_metadata_dict)
        attr_name = "frontend_id"
        with pytest.raises(ValidationError):
            setattr(meta, attr_name, 99)

    def test_metadata_filename_properties(self) -> None:
        """Verify filename helper properties on ProblemMetadata."""
        meta = ProblemMetadata(
            frontend_id=1,
            question_id=1,
            title="Two Sum",
            slug="two-sum",
            difficulty="Easy",
            is_paid_only=False,
        )
        assert meta.slug_snake == "two_sum"
        assert meta.header_filename == "two_sum.hpp"
        assert meta.source_filename == "two_sum.cpp"
        assert meta.test_filename == "two_sum_test.cpp"
        assert meta.python_module_filename == "two_sum.py"
        assert meta.python_test_filename == "test_two_sum.py"
        assert meta.rust_module_filename == "two_sum.rs"
        assert meta.rust_test_filename == "two_sum_test.rs"

    @pytest.mark.unit
    @given(meta=_METADATA_STRATEGY)
    def test_metadata_invariants_property(self, meta: ProblemMetadata) -> None:
        """Verify metadata round-trip, filename invariants, and immutability."""
        expected_snake = meta.slug.replace("-", "_")
        assert meta.slug_snake == expected_snake
        assert "-" not in meta.slug_snake
        assert meta.header_filename == f"{expected_snake}.hpp"
        assert meta.source_filename == f"{expected_snake}.cpp"
        assert meta.test_filename == f"{expected_snake}_test.cpp"
        assert meta.python_module_filename == f"{expected_snake}.py"
        assert meta.python_test_filename == f"test_{expected_snake}.py"
        assert meta.rust_module_filename == f"{expected_snake}.rs"
        assert meta.rust_test_filename == f"{expected_snake}_test.rs"

        assert ProblemMetadata.model_validate(meta.model_dump()) == meta
        assert ProblemMetadata.model_validate_json(meta.model_dump_json()) == meta
        attr_name = "slug"
        with pytest.raises(ValidationError):
            setattr(meta, attr_name, "mutated-slug")

    @pytest.mark.unit
    @given(
        bad_id=st.text(
            alphabet=st.characters(categories=("L",)),
            min_size=1,
            max_size=16,
        ),
    )
    def test_non_integer_ids_always_rejected(self, bad_id: str) -> None:
        """Verify arbitrary alphabetic strings fail integer ID validation."""
        payload: dict[str, Any] = {
            "frontend_id": bad_id,
            "question_id": 1,
            "title": "Two Sum",
            "slug": "two-sum",
            "difficulty": "Easy",
            "is_paid_only": False,
        }
        with pytest.raises(ValidationError):
            ProblemMetadata.model_validate(payload)


class TestProblemDetails:
    """Unit tests for ProblemDetails schema, inheritance, and serialization."""

    def test_valid_details_instantiation(
        self,
        sample_problem_details: ProblemDetails,
    ) -> None:
        """Verify ProblemDetails instantiates and populates all extended fields."""
        assert sample_problem_details.frontend_id == 2
        assert sample_problem_details.cpp_snippet.startswith("/**")
        assert "class Solution" in sample_problem_details.cpp_snippet
        assert len(sample_problem_details.constraints) == 3
        assert sample_problem_details.sample_test_case == "[2,4,3]\n[5,6,4]"
        assert len(sample_problem_details.example_test_cases) == 3

    def test_details_defaults(self) -> None:
        """Verify optional snippet and list fields default cleanly when omitted."""
        p = ProblemDetails(
            frontend_id=1,
            question_id=1,
            title="Two Sum",
            slug="two-sum",
            difficulty="Easy",
            is_paid_only=False,
            description_markdown="# Two Sum",
            cpp_snippet="class Solution {};",
            sample_test_case="[2,7,11,15]\n9",
        )
        assert not p.python_snippet
        assert not p.rust_snippet
        assert p.example_test_cases == []
        assert p.constraints == []
        assert p.topic_tags == []
        assert p.hints == []

    def test_computed_properties(
        self,
        sample_problem_details: ProblemDetails,
    ) -> None:
        """Verify slug_snake and filename helper properties across languages."""
        assert sample_problem_details.slug_snake == "add_two_numbers"
        assert sample_problem_details.header_filename == "add_two_numbers.hpp"
        assert sample_problem_details.source_filename == "add_two_numbers.cpp"
        assert sample_problem_details.test_filename == "add_two_numbers_test.cpp"
        assert sample_problem_details.rust_module_filename == "add_two_numbers.rs"
        assert sample_problem_details.rust_test_filename == "add_two_numbers_test.rs"

    def test_roundtrip_serialization(
        self,
        sample_problem_details: ProblemDetails,
    ) -> None:
        """Verify dict and JSON round-trip serialization maintains full fidelity."""
        dumped = sample_problem_details.model_dump()
        assert dumped["frontend_id"] == 2
        assert dumped["slug"] == "add-two-numbers"

        reconstructed = ProblemDetails.model_validate(dumped)
        assert reconstructed == sample_problem_details

        json_str = sample_problem_details.model_dump_json()
        assert isinstance(json_str, str)
        reconstructed_json = ProblemDetails.model_validate_json(json_str)
        assert reconstructed_json == sample_problem_details

    @pytest.mark.unit
    @given(details=_DETAILS_STRATEGY)
    def test_details_roundtrip_property(self, details: ProblemDetails) -> None:
        """Verify arbitrary ProblemDetails instances round-trip via dict and JSON."""
        assert ProblemDetails.model_validate(details.model_dump()) == details
        assert ProblemDetails.model_validate_json(details.model_dump_json()) == details
