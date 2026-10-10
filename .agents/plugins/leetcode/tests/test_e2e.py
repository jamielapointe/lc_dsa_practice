"""End-to-end verification tests for the LeetCode Antigravity plugin and repository layout.

Tiers:
    1. Plugin manifest and MCP configuration
    2. Skill specification (language selection and per-language references)
    3. Shared C++ data structures
    4. Reference problems exist in both languages and are registered with the build system
    5. Live MCP client invariants (opt-in with ``-m live``)
"""

from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

from leetcode_mcp.client import LeetCodeClient
from leetcode_mcp.models import ProblemDetails


REPO_ROOT = Path(__file__).resolve().parents[4]
PLUGIN_ROOT = REPO_ROOT / '.agents' / 'plugins' / 'leetcode'
SKILL_ROOT = PLUGIN_ROOT / 'skills' / 'leetcode-setup'


class TestTier1PluginManifest:
    """Tier 1: validation of plugin.json and mcp_config.json."""

    def test_plugin_json_structure(self) -> None:
        """The manifest advertises both languages."""
        data = json.loads((PLUGIN_ROOT / 'plugin.json').read_text(encoding='utf-8'))
        assert data['name'] == 'leetcode'
        for key in ('displayName', 'version', 'description'):
            assert key in data
        prompts = ' '.join(data['suggestedPrompts'])
        assert 'C++' in prompts
        assert 'Python' in prompts
        assert (PLUGIN_ROOT / data['logo']).is_file()

    def test_mcp_config_launches_through_pixi(self) -> None:
        """The MCP server must be launched through Pixi, never a bare interpreter or uv."""
        data = json.loads((PLUGIN_ROOT / 'mcp_config.json').read_text(encoding='utf-8'))
        server = data['mcpServers']['leetcode']
        assert server['command'] == 'pixi'
        assert 'leetcode-mcp' in server['args']

    def test_no_plugin_local_environment_remains(self) -> None:
        """The plugin is folded into the root Pixi workspace."""
        assert not (PLUGIN_ROOT / 'pyproject.toml').exists()
        assert not (PLUGIN_ROOT / 'uv.lock').exists()


class TestTier2SkillSpecification:
    """Tier 2: validation of the leetcode-setup skill."""

    def test_skill_frontmatter(self) -> None:
        """SKILL.md has valid frontmatter."""
        content = (SKILL_ROOT / 'SKILL.md').read_text(encoding='utf-8')
        match = re.match(r'^---\n(.*?)\n---', content, re.DOTALL)
        assert match is not None
        assert 'name: leetcode-setup' in match.group(1)
        assert 'description:' in match.group(1)

    def test_skill_requires_explicit_language(self) -> None:
        """The skill must make the user choose cpp or python and link both references."""
        content = (SKILL_ROOT / 'SKILL.md').read_text(encoding='utf-8')
        assert 'Determine the language' in content
        assert 'Never guess or default' in content
        assert 'references/cpp.md' in content
        assert 'references/python.md' in content
        assert 'github-mcp-server' in content

    @pytest.mark.parametrize(
        ('reference', 'required'),
        [
            (
                'cpp.md',
                [
                    'static_cast<void>',
                    '-Werror',
                    'ScopedLinkedList',
                    'ASSERT_NE',
                    'add_library',
                    'add_project_test',
                    'pixi run',
                ],
            ),
            ('python.md', ['Solution', 'pytest.mark.stress', 'del ', 'mypy', 'ruff', 'pixi run']),
        ],
    )
    def test_reference_contents(self, reference: str, required: list[str]) -> None:
        """Each language reference documents its mandatory safeguards."""
        content = (SKILL_ROOT / 'references' / reference).read_text(encoding='utf-8')
        for token in required:
            assert token in content, f'{reference} is missing {token!r}'


class TestTier3SharedDataStructures:
    """Tier 3: canonical C++ shared data structures."""

    def test_list_node_header_contracts(self) -> None:
        """list_node.hpp lives under src/include/leet_code and exposes the documented API."""
        code = (REPO_ROOT / 'src' / 'include' / 'leet_code' / 'list_node.hpp').read_text(encoding='utf-8')
        assert '#pragma once' in code
        assert 'namespace leet_code' in code
        for symbol in (
            'struct ListNode',
            'class ScopedLinkedList',
            'create_linked_list',
            'linked_list_to_vector',
            'free_linked_list',
        ):
            assert symbol in code


class TestTier4ReferenceProblems:
    """Tier 4: reference problems exist in both languages and are registered."""

    def test_cpp_two_sum_artifacts_and_cmake_registration(self) -> None:
        """C++ files live in the new tree and are registered in CMake."""
        assert (REPO_ROOT / 'src' / 'include' / 'leet_code' / 'two_sum.hpp').is_file()
        assert (REPO_ROOT / 'src' / 'cpp' / 'leet_code' / 'two_sum.cpp').is_file()
        assert (REPO_ROOT / 'test' / 'cpp' / 'leet_code' / 'two_sum_test.cpp').is_file()
        src_cmake = (REPO_ROOT / 'src' / 'cpp' / 'leet_code' / 'CMakeLists.txt').read_text(encoding='utf-8')
        test_cmake = (REPO_ROOT / 'test' / 'cpp' / 'leet_code' / 'CMakeLists.txt').read_text(encoding='utf-8')
        assert 'add_library(two_sum STATIC two_sum.cpp)' in src_cmake
        assert 'add_library(leet_code::two_sum ALIAS two_sum)' in src_cmake
        assert 'two_sum_test' in test_cmake

    def test_python_two_sum_artifacts(self) -> None:
        """The Python port and its tests exist."""
        assert (REPO_ROOT / 'src' / 'python' / 'leet_code' / '__init__.py').is_file()
        assert (REPO_ROOT / 'src' / 'python' / 'leet_code' / 'two_sum.py').is_file()
        assert (REPO_ROOT / 'test' / 'python' / 'leet_code' / 'test_two_sum.py').is_file()

    def test_model_naming_helpers_cover_both_languages(self) -> None:
        """ProblemMetadata derives file names for C++ and Python."""
        details = ProblemDetails(
            frontend_id=1,
            question_id=1,
            title='Two Sum',
            slug='two-sum',
            difficulty='Easy',
            is_paid_only=False,
            description_markdown='...',
            cpp_snippet='class Solution {};',
            python_snippet='class Solution: ...',
            sample_test_case='',
        )
        assert details.header_filename == 'two_sum.hpp'
        assert details.source_filename == 'two_sum.cpp'
        assert details.test_filename == 'two_sum_test.cpp'
        assert details.python_module_filename == 'two_sum.py'
        assert details.python_test_filename == 'test_two_sum.py'


class TestTier5LiveMCPProtocol:
    """Tier 5: live API query validation against LeetCode GraphQL (opt-in)."""

    @pytest.mark.live
    async def test_live_client_returns_both_snippets(self) -> None:
        """Problem #2 is fetched with C++ and Python snippets."""
        async with LeetCodeClient() as client:
            details = await client.fetch_problem(2)
        assert isinstance(details, ProblemDetails)
        assert details.slug == 'add-two-numbers'
        assert 'ListNode' in details.cpp_snippet
        assert 'class Solution' in details.python_snippet
        assert len(details.constraints) >= 2
