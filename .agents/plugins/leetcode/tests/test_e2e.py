"""End-to-End (E2E) verification test suite for LeetCode Antigravity Plugin.

Validates all 5 tiers of project architecture and acceptance criteria:
- Tier 1: Plugin Manifest & MCP Configuration (R1, survey_2)
- Tier 2: LeetCode Setup Skill Specification (R3, survey_3)
- Tier 3: Shared C++ Data Structures & ASan RAII Wrappers (M1, survey_1)
- Tier 4: LeetCode #2 C++23 Artifacts, CMake Registration & TDD Red State (R4, AC)
- Tier 5: Live MCP Protocol & Client Invariants (R2, AC)
"""

import json
import re
import subprocess
from pathlib import Path

import pytest

from leetcode_mcp.client import LeetCodeClient
from leetcode_mcp.models import ProblemDetails

REPO_ROOT = Path(__file__).resolve().parents[4]
PLUGIN_ROOT = REPO_ROOT / ".agents" / "plugins" / "leetcode"


class TestTier1PluginManifest:
    """Tier 1: Validation of plugin.json and mcp_config.json manifests."""

    def test_plugin_json_structure(self) -> None:
        manifest_path = PLUGIN_ROOT / "plugin.json"
        assert manifest_path.is_file(), f"Missing plugin.json at {manifest_path}"

        data = json.loads(manifest_path.read_text(encoding="utf-8"))
        assert data.get("name") == "leetcode"
        assert "displayName" in data
        assert "version" in data
        assert "description" in data
        assert isinstance(data.get("suggestedPrompts"), list)
        assert len(data["suggestedPrompts"]) >= 3
        assert "Set up LeetCode #2" in data["suggestedPrompts"]
        assert (PLUGIN_ROOT / data.get("logo", "")).is_file()

    def test_mcp_config_json_structure(self) -> None:
        config_path = PLUGIN_ROOT / "mcp_config.json"
        assert config_path.is_file(), f"Missing mcp_config.json at {config_path}"

        data = json.loads(config_path.read_text(encoding="utf-8"))
        servers = data.get("mcpServers", {})
        assert "leetcode" in servers or "leetcode-mcp" in servers

        server_entry = servers.get("leetcode") or servers.get("leetcode-mcp")
        assert server_entry["command"] == "uv"
        assert "run" in server_entry["args"]
        assert "leetcode_mcp" in " ".join(server_entry["args"])


class TestTier2SkillSpecification:
    """Tier 2: Validation of leetcode-setup SKILL.md specification."""

    def test_skill_file_exists_and_has_valid_frontmatter(self) -> None:
        skill_path = PLUGIN_ROOT / "skills" / "leetcode-setup" / "SKILL.md"
        assert skill_path.is_file(), f"Missing SKILL.md at {skill_path}"

        content = skill_path.read_text(encoding="utf-8")
        assert content.startswith("---")
        # Extract YAML frontmatter
        match = re.match(r"^---\n(.*?)\n---", content, re.DOTALL)
        assert match is not None, "SKILL.md must have valid YAML frontmatter"
        frontmatter = match.group(1)
        assert "name: leetcode-setup" in frontmatter
        assert "description:" in frontmatter

    def test_skill_defines_complete_workflow(self) -> None:
        skill_path = PLUGIN_ROOT / "skills" / "leetcode-setup" / "SKILL.md"
        content = skill_path.read_text(encoding="utf-8")

        # Verify all mandatory steps
        assert "Step 1: Query Problem Data via MCP Server" in content
        assert "Step 2: Shared Data Structure Resolution" in content
        assert "Step 3: Generate C++23 Header" in content
        assert "Step 4: Generate Barebones Source" in content
        assert "Step 5: Generate Comprehensive 4-Tier Google Test Suite" in content
        assert "Step 6: Update CMake Build System" in content
        assert "Step 7: Compilation & Test Verification" in content

        # Verify technical safeguards
        assert "static_cast<void>" in content
        assert "-Werror" in content
        assert "ScopedLinkedList" in content
        assert "ASSERT_NE" in content
        assert "add_library" in content
        assert "add_project_test" in content


class TestTier3SharedDataStructures:
    """Tier 3: Validation of canonical C++ shared data structures."""

    def test_list_node_header_contracts(self) -> None:
        header_path = REPO_ROOT / "include" / "lc_dsa" / "list_node.hpp"
        assert header_path.is_file()

        code = header_path.read_text(encoding="utf-8")
        assert "#pragma once" in code
        assert "namespace lc_dsa" in code
        assert "struct ListNode" in code
        assert "class ScopedLinkedList" in code
        assert "create_linked_list" in code
        assert "linked_list_to_vector" in code
        assert "free_linked_list" in code


class TestTier4LeetCode2ArtifactsAndBuild:
    """Tier 4: Validation of LeetCode #2 files, CMake targets, and TDD Red state."""

    def test_generated_files_exist(self) -> None:
        header = REPO_ROOT / "include" / "lc_dsa" / "add_two_numbers.hpp"
        source = REPO_ROOT / "src" / "add_two_numbers.cpp"
        test_file = REPO_ROOT / "tests" / "add_two_numbers_test.cpp"

        assert header.is_file(), f"Missing {header}"
        assert source.is_file(), f"Missing {source}"
        assert test_file.is_file(), f"Missing {test_file}"

    def test_cmake_registrations(self) -> None:
        src_cmake = (REPO_ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
        test_cmake = (REPO_ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")

        assert "add_library(add_two_numbers STATIC add_two_numbers.cpp)" in src_cmake
        assert "add_library(lc_dsa::add_two_numbers ALIAS add_two_numbers)" in src_cmake
        assert "target_compile_features(add_two_numbers PUBLIC cxx_std_23)" in src_cmake
        assert "add_project_test" in test_cmake
        assert "add_two_numbers_test" in test_cmake

    def test_cpp_compilation_and_clean_tdd_red_failure(self) -> None:
        # 1. Compile test target with dev-debug preset
        build_res = subprocess.run(
            ["cmake", "--build", "--preset", "dev-debug", "--target", "add_two_numbers_test"],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
        )
        assert build_res.returncode == 0, f"Compilation failed: {build_res.stderr}"

        # 2. Run test executable: must fail cleanly on assertion (exit code != 0, 0 segfaults)
        test_bin = REPO_ROOT / "build" / "bin" / "add_two_numbers_test"
        assert test_bin.is_file()

        run_res = subprocess.run(
            [str(test_bin)],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
        )
        assert run_res.returncode == 1, (
            f"Expected exit code 1 (clean assertion failure), got {run_res.returncode}"
        )
        assert "FAILED" in run_res.stdout
        assert "Failure" in run_res.stdout
        assert "Expected: (result) != (nullptr)" in run_res.stdout
        # Ensure no segfault or ASan crash
        assert "Segmentation fault" not in run_res.stderr
        assert "AddressSanitizer" not in run_res.stderr


class TestTier5LiveMCPProtocol:
    """Tier 5: Live API query validation against LeetCode GraphQL."""

    @pytest.mark.live
    @pytest.mark.asyncio
    async def test_live_mcp_client_query_problem_2(self) -> None:
        async with LeetCodeClient() as client:
            details = await client.fetch_problem(2)
            assert isinstance(details, ProblemDetails)
            assert details.frontend_id == 2
            assert details.slug == "add-two-numbers"
            assert details.title == "Add Two Numbers"
            assert "ListNode" in details.cpp_snippet
            assert len(details.constraints) >= 2
            assert details.slug_snake == "add_two_numbers"
            assert details.header_filename == "add_two_numbers.hpp"
            assert details.source_filename == "add_two_numbers.cpp"
            assert details.test_filename == "add_two_numbers_test.cpp"
