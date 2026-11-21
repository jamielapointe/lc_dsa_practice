"""Validation suite for Antigravity 2.34.0 workspace rules and AGENTS.md.

Verifies repository compliance with Antigravity 2.34.0 rule discovery, frontmatter,
glob portability, inline-include expansion, and byte-budget invariants:
- Root ``AGENTS.md`` has no YAML frontmatter and stays well below the 24,000-byte cap.
- ``.agents/rules/`` is a flat 1-level directory (no nested subdirectories).
- No subfolder ``AGENTS.md`` or ``GEMINI.md`` files exist below workspace root.
- Every ``.agents/rules/*.md`` file has valid YAML frontmatter, a valid ``trigger``,
  required ``description`` / ``globs`` fields, portable comma-separated globs without
  brace expansion, and expanded byte size strictly under 24,000 bytes.
"""

import re
from pathlib import Path
from typing import cast

import pytest
import yaml


REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
AGENTS_MD_PATH = REPO_ROOT / "AGENTS.md"
RULES_DIR = REPO_ROOT / ".agents" / "rules"

ANTIGRAVITY_RULE_MAX_BYTES = 24_000
AGENTS_MD_MIN_HEADROOM_BYTES = 1_024
UTF8_BOM = b"\xef\xbb\xbf"

VALID_TRIGGERS = frozenset({"always_on", "model_decision", "glob", "manual"})
EXPECTED_RULE_FILES = frozenset({
    "benchmarks.md",
    "cpp-and-c23.md",
    "crubit-interop.md",
    "python.md",
    "rust.md",
    "systems-engineering.md",
})
SUBFOLDER_RULE_SCAN_ROOTS = (
    "src",
    "test",
    "examples",
    "benchmarks",
    "docs",
    "notebooks",
    "scripts",
)
SUBFOLDER_RULE_NAMES = frozenset({"AGENTS.md", "GEMINI.md"})
FENCED_CODE_BLOCK_RE = re.compile(r"(?m)^```.*?^```[ \t]*$", re.DOTALL)
INLINE_CODE_SPAN_RE = re.compile(r"`[^`\n]+`")
INLINE_INCLUDE_RE = re.compile(r"@\[[^\]]*\]\(([^)]+)\)")


def _split_frontmatter(text: str) -> tuple[dict[str, object] | None, str]:
    """Splits optional leading YAML frontmatter from Markdown body text.

    Args:
        text: UTF-8 Markdown document text.

    Returns:
        Tuple of ``(frontmatter_mapping, body_text)`` where ``frontmatter_mapping``
        is ``None`` if the document does not start with ``---`` frontmatter.

    Raises:
        ValueError: If frontmatter is unclosed or does not parse as a YAML mapping.
    """
    lines = text.splitlines(keepends=True)
    if not lines or lines[0].strip() != "---":
        return None, text

    closing_idx: int | None = None
    for idx in range(1, len(lines)):
        if lines[idx].strip() == "---":
            closing_idx = idx
            break

    if closing_idx is None:
        msg = "YAML frontmatter opening '---' has no matching closing '---'"
        raise ValueError(msg)

    raw_yaml = "".join(lines[1:closing_idx])
    body = "".join(lines[closing_idx + 1 :])
    try:
        parsed: object = yaml.safe_load(raw_yaml)
    except yaml.YAMLError as err:
        msg = f"Invalid YAML frontmatter: {err}"
        raise ValueError(msg) from err

    if not isinstance(parsed, dict):
        msg = "YAML frontmatter must deserialize to a mapping"
        raise ValueError(msg)

    raw_mapping = cast("dict[object, object]", parsed)
    mapping: dict[str, object] = {}
    for key, value in raw_mapping.items():
        if not isinstance(key, str):
            msg = f"YAML frontmatter key must be a string (got {type(key).__name__})"
            raise ValueError(msg)
        mapping[key] = value
    return mapping, body


def _resolve_inline_include(
    raw_target: str,
    base_dir: Path,
    seen: frozenset[Path],
) -> str:
    """Resolves and recursively expands a single ``@[label](path)`` include.

    Args:
        raw_target: Target path string extracted from ``@[label](path)``.
        base_dir: Directory used to resolve relative include paths.
        seen: Set of already-visited resolved paths to prevent include cycles.

    Returns:
        Expanded Markdown body of the included file (with frontmatter stripped).

    Raises:
        FileNotFoundError: If the included target file does not exist.
        ValueError: If a cyclic include is detected.
    """
    target_path = Path(raw_target)
    if not target_path.is_absolute():
        target_path = (base_dir / target_path).resolve()
    if target_path in seen:
        msg = f"Cyclic inline include detected: {target_path}"
        raise ValueError(msg)
    if not target_path.is_file():
        msg = f"Inline include target does not exist: {raw_target}"
        raise FileNotFoundError(msg)
    included_text = target_path.read_text(encoding="utf-8")
    _, included_body = _split_frontmatter(included_text)
    return _expand_inline_includes(
        included_body,
        target_path.parent,
        seen=seen | {target_path},
    )


def _expand_inline_includes(
    text: str,
    base_dir: Path,
    *,
    seen: frozenset[Path] = frozenset(),
) -> str:
    """Expands Antigravity ``@[label](path)`` inline file includes recursively.

    Mirrors Antigravity's pre-cap expansion behavior by stripping frontmatter from
    included files and guarding against cycles.

    Args:
        text: Raw Markdown text potentially containing ``@[label](path)`` directives.
        base_dir: Directory used to resolve relative include paths.
        seen: Set of already-visited resolved paths to prevent include cycles.

    Returns:
        Markdown text with all active inline includes outside code blocks/spans
        replaced by their body contents.
    """
    preserved_spans: list[str] = []

    def _stash(match: re.Match[str]) -> str:
        placeholder = f"\x00CODE_SPAN_{len(preserved_spans)}\x00"
        preserved_spans.append(match.group(0))
        return placeholder

    masked = FENCED_CODE_BLOCK_RE.sub(_stash, text)
    masked = INLINE_CODE_SPAN_RE.sub(_stash, masked)
    expanded = INLINE_INCLUDE_RE.sub(
        lambda match: _resolve_inline_include(
            match.group(1).strip(),
            base_dir,
            seen,
        ),
        masked,
    )
    for idx, original in enumerate(preserved_spans):
        expanded = expanded.replace(f"\x00CODE_SPAN_{idx}\x00", original)
    return expanded


def _validate_glob_field(raw_globs: object) -> list[str]:
    """Validates an Antigravity ``globs`` frontmatter value.

    Args:
        raw_globs: Parsed YAML value of ``globs`` (or ``glob``).

    Returns:
        List of stripped non-empty glob patterns.

    Raises:
        ValueError: If ``raw_globs`` is missing, empty, or uses brace expansion.
    """
    if not isinstance(raw_globs, str) or not raw_globs.strip():
        msg = "glob-triggered rule must define a non-empty string 'globs' field"
        raise ValueError(msg)

    patterns = [part.strip() for part in raw_globs.split(",")]
    if not patterns or any(not pattern for pattern in patterns):
        msg = f"Invalid empty pattern in globs string: {raw_globs!r}"
        raise ValueError(msg)

    for pattern in patterns:
        if "{" in pattern or "}" in pattern:
            msg = (
                f"Brace expansion is disallowed in Antigravity rule globs for "
                f"portability: {pattern!r}"
            )
            raise ValueError(msg)

    return patterns


def _validate_rule_document(
    content: bytes,
    base_dir: Path,
) -> dict[str, object]:
    """Validates an Antigravity 2.34.0 ``.agents/rules/*.md`` document.

    Args:
        content: Raw file bytes of the rule document.
        base_dir: Parent directory for resolving any ``@[label](path)`` includes.

    Returns:
        Validated frontmatter dictionary.

    Raises:
        ValueError: If any Antigravity 2.34.0 rule invariant is violated.
    """
    if content.startswith(UTF8_BOM):
        msg = "Rule file must not contain a UTF-8 BOM"
        raise ValueError(msg)

    if len(content) >= ANTIGRAVITY_RULE_MAX_BYTES:
        msg = (
            f"Rule file size ({len(content)} bytes) must be strictly less than "
            f"{ANTIGRAVITY_RULE_MAX_BYTES} bytes"
        )
        raise ValueError(msg)

    text = content.decode("utf-8")
    frontmatter, _ = _split_frontmatter(text)
    if frontmatter is None:
        msg = "Rule file must begin with YAML frontmatter delimited by '---'"
        raise ValueError(msg)

    trigger = frontmatter.get("trigger")
    if not isinstance(trigger, str) or trigger not in VALID_TRIGGERS:
        msg = f"Invalid trigger {trigger!r}; expected one of {sorted(VALID_TRIGGERS)}"
        raise ValueError(msg)

    description = frontmatter.get("description")
    if not isinstance(description, str) or not description.strip():
        msg = "Rule frontmatter must include a non-empty string 'description'"
        raise ValueError(msg)

    if trigger == "glob":
        raw_globs = frontmatter.get("globs", frontmatter.get("glob"))
        _validate_glob_field(raw_globs)

    expanded_bytes = len(_expand_inline_includes(text, base_dir).encode("utf-8"))
    if expanded_bytes >= ANTIGRAVITY_RULE_MAX_BYTES:
        msg = (
            f"Expanded rule size ({expanded_bytes} bytes) exceeds "
            f"{ANTIGRAVITY_RULE_MAX_BYTES}-byte Antigravity limit"
        )
        raise ValueError(msg)

    return frontmatter


@pytest.mark.unit
class TestRepositoryAgentRules:
    """Verifies live repository AGENTS.md and .agents/rules/*.md invariants."""

    def test_agents_md_has_no_frontmatter_and_respects_budget(self) -> None:
        """Verify root AGENTS.md is plain Markdown well under 24,000 bytes."""
        assert AGENTS_MD_PATH.is_file(), "Root AGENTS.md must exist"
        raw_bytes = AGENTS_MD_PATH.read_bytes()
        assert not raw_bytes.startswith(UTF8_BOM), "AGENTS.md must not have a UTF-8 BOM"

        text = raw_bytes.decode("utf-8")
        frontmatter, _ = _split_frontmatter(text)
        assert frontmatter is None, "Root AGENTS.md must not contain YAML frontmatter"

        expanded_bytes = len(
            _expand_inline_includes(text, AGENTS_MD_PATH.parent).encode("utf-8")
        )
        assert expanded_bytes < ANTIGRAVITY_RULE_MAX_BYTES, (
            f"AGENTS.md expanded size ({expanded_bytes} bytes) exceeds "
            f"{ANTIGRAVITY_RULE_MAX_BYTES} bytes"
        )
        headroom = ANTIGRAVITY_RULE_MAX_BYTES - expanded_bytes
        assert headroom >= AGENTS_MD_MIN_HEADROOM_BYTES, (
            f"AGENTS.md headroom ({headroom} bytes) is below "
            f"{AGENTS_MD_MIN_HEADROOM_BYTES} bytes"
        )

    def test_rules_directory_is_flat_one_level(self) -> None:
        """Verify .agents/rules/ contains only immediate .md files (no subdirs)."""
        assert RULES_DIR.is_dir(), ".agents/rules/ directory must exist"

        entries = sorted(RULES_DIR.iterdir())
        assert entries, ".agents/rules/ must contain at least one rule file"

        subdirs = [entry for entry in entries if entry.is_dir()]
        assert not subdirs, (
            f".agents/rules/ must be flat (no nested directories): {subdirs}"
        )

        non_markdown = [entry for entry in entries if entry.suffix != ".md"]
        assert not non_markdown, (
            f".agents/rules/ must contain only .md files: {non_markdown}"
        )

        actual_names = {entry.name for entry in entries}
        missing = EXPECTED_RULE_FILES - actual_names
        assert not missing, f"Missing expected rule files: {sorted(missing)}"

    def test_no_nested_subfolder_agents_or_gemini_md(self) -> None:
        """Verify no subfolder AGENTS.md or GEMINI.md files exist under workspace."""
        nested_files = [
            candidate
            for root_name in SUBFOLDER_RULE_SCAN_ROOTS
            if (REPO_ROOT / root_name).is_dir()
            for candidate in (REPO_ROOT / root_name).rglob("*.md")
            if candidate.name in SUBFOLDER_RULE_NAMES
        ]
        assert not nested_files, (
            "Antigravity does not auto-load subfolder AGENTS.md/GEMINI.md below "
            f"workspace root; move into .agents/rules/: {nested_files}"
        )

    def test_every_workspace_rule_passes_antigravity_contract(self) -> None:
        """Verify every .agents/rules/*.md satisfies frontmatter, globs, and limits."""
        rule_files = sorted(RULES_DIR.glob("*.md"))
        assert rule_files, "Expected at least one rule in .agents/rules/"

        for rule_path in rule_files:
            frontmatter = _validate_rule_document(
                rule_path.read_bytes(),
                rule_path.parent,
            )
            assert frontmatter["trigger"] in VALID_TRIGGERS

    def test_agents_md_indexes_every_workspace_rule(self) -> None:
        """Verify root AGENTS.md references every scoped rule in .agents/rules/."""
        agents_text = AGENTS_MD_PATH.read_text(encoding="utf-8")
        for rule_path in sorted(RULES_DIR.glob("*.md")):
            relative_ref = f".agents/rules/{rule_path.name}"
            assert relative_ref in agents_text, (
                f"AGENTS.md progressive-disclosure index must reference {relative_ref}"
            )


@pytest.mark.unit
class TestAgentRuleValidatorBoundaryCases:
    """Unit tests covering edge cases and failure modes of rule validation."""

    def test_rejects_missing_frontmatter(self, tmp_path: Path) -> None:
        """Reject a rule file that omits YAML frontmatter."""
        with pytest.raises(ValueError, match="YAML frontmatter"):
            _validate_rule_document(b"# Missing frontmatter\n", tmp_path)

    def test_rejects_camelcase_or_unknown_trigger(self, tmp_path: Path) -> None:
        """Reject unrecognized or camelCase triggers that Antigravity discards."""
        doc = b"---\ntrigger: alwaysOn\ndescription: Invalid camelCase trigger.\n---\n\n# Body\n"
        with pytest.raises(ValueError, match="Invalid trigger"):
            _validate_rule_document(doc, tmp_path)

    def test_rejects_brace_expansion_in_globs(self, tmp_path: Path) -> None:
        """Reject glob brace expansion syntax such as ``{hpp,cpp}``."""
        doc = (
            b"---\n"
            b"trigger: glob\n"
            b"description: Non-portable glob.\n"
            b'globs: "src/**/*.{hpp,cpp}"\n'
            b"---\n\n# Body\n"
        )
        with pytest.raises(ValueError, match="Brace expansion"):
            _validate_rule_document(doc, tmp_path)

    def test_rejects_inline_include_overflowing_24000_bytes(
        self,
        tmp_path: Path,
    ) -> None:
        """Reject a small rule whose @[label](path) include exceeds 24,000 bytes."""
        large_file = tmp_path / "bloated.md"
        large_file.write_bytes(b"x" * ANTIGRAVITY_RULE_MAX_BYTES)
        doc = (
            b"---\n"
            b"trigger: model_decision\n"
            b"description: Pulls in oversized file.\n"
            b"---\n\n"
            b"@[bloat](bloated.md)\n"
        )
        with pytest.raises(ValueError, match="Expanded rule size"):
            _validate_rule_document(doc, tmp_path)

    def test_ignores_inline_include_syntax_inside_code_spans(
        self,
        tmp_path: Path,
    ) -> None:
        """Ignore literal ``@[label](path)`` examples inside Markdown code spans."""
        doc = (
            b"---\n"
            b"trigger: model_decision\n"
            b"description: Mentions include syntax in backticks.\n"
            b"---\n\n"
            b"Do not use `@[label](nonexistent.md)` to split rules.\n"
        )
        frontmatter = _validate_rule_document(doc, tmp_path)
        assert frontmatter["trigger"] == "model_decision"

    def test_rejects_utf8_bom_in_rule(self, tmp_path: Path) -> None:
        """Reject rule files prefixed with a UTF-8 BOM."""
        doc = (
            UTF8_BOM + b"---\ntrigger: manual\ndescription: BOM test.\n---\n\n# Body\n"
        )
        with pytest.raises(ValueError, match="UTF-8 BOM"):
            _validate_rule_document(doc, tmp_path)
