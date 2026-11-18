"""E2E structural and toolchain invariants for CMake 4.5+ and Presets v13."""

import json
import re
import shutil
import subprocess
import tomllib
from pathlib import Path
from typing import cast

import pytest
import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]

FORBIDDEN_NON_TARGET_DIRECTIVE_RE = re.compile(
    r"(?<![A-Za-z0-9_])("
    r"include_directories"
    r"|add_compile_options"
    r"|add_definitions"
    r"|add_compile_definitions"
    r"|link_directories"
    r"|link_libraries"
    r"|add_link_options"
    r")\s*\("
)

REQUIRED_CONFIGURE_PRESETS = frozenset({
    "dev-debug",
    "ci-debug",
    "dev-release",
    "asan",
    "ubsan",
    "tsan",
    "coverage",
    "benchmark",
    "interop-debug",
    "interop-release",
    "sysroot-release",
    "cross-aarch64",
    "cross-riscv64",
})

REQUIRED_WORKFLOW_PRESETS = frozenset({
    "dev-debug",
    "ci-debug",
    "dev-release",
    "asan",
    "ubsan",
    "tsan",
    "coverage",
    "benchmark",
    "interop-debug",
    "interop-release",
})


def _strip_cmake_comments(content: str) -> str:
    """Strips line comments from CMake source while preserving string literals.

    Args:
        content: Raw CMake file text.

    Returns:
        CMake source text with unquoted ``#`` comments removed.
    """
    cleaned_lines: list[str] = []
    for line in content.splitlines():
        in_quotes = False
        escaped = False
        comment_idx: int | None = None
        for idx, ch in enumerate(line):
            if escaped:
                escaped = False
                continue
            if ch == "\\":
                escaped = True
                continue
            if ch == '"':
                in_quotes = not in_quotes
                continue
            if ch == "#" and not in_quotes:
                comment_idx = idx
                break
        cleaned_lines.append(line if comment_idx is None else line[:comment_idx])
    return "\n".join(cleaned_lines)


def _load_json_dict(path: Path) -> dict[str, object]:
    """Loads a JSON object from disk with runtime type validation.

    Args:
        path: Path to the JSON file.

    Returns:
        Parsed top-level JSON dictionary.
    """
    raw = cast("object", json.loads(path.read_text(encoding="utf-8")))
    assert isinstance(raw, dict), f"Expected JSON object in {path}"
    return cast("dict[str, object]", raw)


def _load_yaml_dict(path: Path) -> dict[str, object]:
    """Loads a YAML mapping from disk with runtime type validation.

    Args:
        path: Path to the YAML file.

    Returns:
        Parsed top-level YAML mapping.
    """
    raw = cast("object", yaml.safe_load(path.read_text(encoding="utf-8")))
    assert isinstance(raw, dict), f"Expected YAML mapping in {path}"
    return cast("dict[str, object]", raw)


def _load_toml_dict(path: Path) -> dict[str, object]:
    """Loads a TOML table from disk with runtime type validation.

    Args:
        path: Path to the TOML file.

    Returns:
        Parsed top-level TOML dictionary.
    """
    raw = cast("object", tomllib.loads(path.read_text(encoding="utf-8")))
    assert isinstance(raw, dict), f"Expected TOML table in {path}"
    return cast("dict[str, object]", raw)


def _parse_min_version(spec: str) -> tuple[int, ...]:
    """Extracts a ``>=`` version floor tuple from a dependency constraint string.

    Args:
        spec: Dependency version specifier (for example ``>=4.5.0``).

    Returns:
        Tuple of integer version components.
    """
    match = re.search(r">=\s*(\d+(?:\.\d+)*)", spec)
    assert match is not None, f"Expected '>=' version floor in {spec!r}"
    return tuple(int(part) for part in match.group(1).split("."))


def _collect_cmake_files() -> list[Path]:
    """Collects all tracked CMake scripts and CMakeLists files under the repo.

    Returns:
        Sorted list of root, module, and subdirectory CMake file paths.
    """
    files: list[Path] = [REPO_ROOT / "CMakeLists.txt"]
    cmake_dir = REPO_ROOT / "cmake"
    if cmake_dir.is_dir():
        files.extend(sorted(cmake_dir.rglob("*.cmake")))
    for tree in ("src", "test", "benchmarks", "examples"):
        tree_dir = REPO_ROOT / tree
        if tree_dir.is_dir():
            files.extend(sorted(tree_dir.rglob("CMakeLists.txt")))
    return files


def _extract_requirement_spec(requirements: list[object], package: str) -> str:
    """Finds a PEP 508 requirement string for ``package`` inside a TOML list.

    Args:
        requirements: List of requirement entries from ``pyproject.toml``.
        package: Normalized package name prefix to match.

    Returns:
        Matching requirement string.

    Raises:
        AssertionError: If no matching dependency entry is present.
    """
    pattern = re.compile(rf"^{re.escape(package)}(?:$|[\s\[<>=!~])", re.IGNORECASE)
    for item in requirements:
        if isinstance(item, str) and pattern.match(item.strip()):
            return item.strip()
    msg = f"Missing dependency specification for {package!r}"
    raise AssertionError(msg)


@pytest.mark.unit
def test_root_cmakelists_cmake45_features() -> None:
    """Verify root CMakeLists.txt uses CMake 4.5+ diagnostics, CPS, and SBOM."""
    root_cmake = REPO_ROOT / "CMakeLists.txt"
    content = root_cmake.read_text(encoding="utf-8")

    assert re.search(r"cmake_minimum_required\(\s*VERSION\s+4\.5\b", content), (
        "Root CMakeLists.txt must require cmake_minimum_required(VERSION 4.5)"
    )
    assert re.search(r"\bCOMPAT_VERSION\b", content), (
        "Root project() call must define COMPAT_VERSION"
    )
    assert re.search(r'SPDX_LICENSE\s+"Apache-2\.0"', content), (
        'Root project() call must define SPDX_LICENSE "Apache-2.0"'
    )
    assert re.search(
        r"cmake_diagnostic\(\s*SET\s+CMD_NON_TARGET_DIRECTIVE\s+SEND_ERROR\s*\)",
        content,
    ), "Root CMakeLists.txt must set CMD_NON_TARGET_DIRECTIVE to SEND_ERROR"
    assert re.search(r"\binstall\(\s*PACKAGE_INFO\b", content), (
        "Root CMakeLists.txt must configure install(PACKAGE_INFO ...)"
    )
    assert re.search(
        r"\binstall\(\s*SBOM\b[\s\S]*?\bFORMAT\s+spdx-3\.0\.1\+json\b",
        content,
    ), "Root CMakeLists.txt must configure install(SBOM ... FORMAT spdx-3.0.1+json)"

    cmake_files = [root_cmake, *sorted((REPO_ROOT / "cmake").rglob("*.cmake"))]
    for path in cmake_files:
        text = path.read_text(encoding="utf-8")
        assert "CXX_MODULE_STD" not in text, (
            f"Obsolete CXX_MODULE_STD found in {path.relative_to(REPO_ROOT)}"
        )


@pytest.mark.unit
def test_zero_forbidden_non_target_directives() -> None:
    """Verify zero non-target CMake directives appear across the repository."""
    violations: list[str] = []
    for cmake_path in _collect_cmake_files():
        stripped = _strip_cmake_comments(cmake_path.read_text(encoding="utf-8"))
        for match in FORBIDDEN_NON_TARGET_DIRECTIVE_RE.finditer(stripped):
            rel = cmake_path.relative_to(REPO_ROOT)
            violations.append(f"{rel}: forbidden directive {match.group(1)}(")

    assert not violations, "Non-target CMake directives found:\n" + "\n".join(
        violations
    )


@pytest.mark.unit
def test_cmake_presets_v13_structure() -> None:
    """Verify CMakePresets.json conforms to Schema v13 and exposes required presets."""
    presets_path = REPO_ROOT / "CMakePresets.json"
    data = _load_json_dict(presets_path)

    assert data.get("version") == 13, "CMakePresets.json must use Schema version 13"
    assert data.get("cmakeMinimumRequired") == {
        "major": 4,
        "minor": 5,
        "patch": 0,
    }, "cmakeMinimumRequired must be 4.5.0"

    raw_configure = data.get("configurePresets")
    assert isinstance(raw_configure, list)
    configure_by_name: dict[str, dict[str, object]] = {}
    for entry in cast("list[object]", raw_configure):
        assert isinstance(entry, dict)
        typed_entry = cast("dict[str, object]", entry)
        name = typed_entry.get("name")
        assert isinstance(name, str)
        configure_by_name[name] = typed_entry

    assert "base" in configure_by_name
    assert "pixi-base" in configure_by_name
    assert configure_by_name["base"].get("hidden") is True
    assert configure_by_name["pixi-base"].get("hidden") is True
    assert configure_by_name["pixi-base"].get("inherits") == "base"
    assert (
        configure_by_name["base"].get("installDir")
        == "${sourceDir}/build/${presetName}/install"
    ), '"base" preset must define a build-local installDir'

    base_warnings = cast("dict[str, object]", configure_by_name["base"].get("warnings"))
    base_errors = cast("dict[str, object]", configure_by_name["base"].get("errors"))
    assert base_warnings.get("experimental") is True
    assert base_errors.get("experimental") is True
    assert base_errors.get("installAbsoluteDestination") is True

    base_cache = cast(
        "dict[str, object]", configure_by_name["base"].get("cacheVariables")
    )
    assert base_cache.get("CMAKE_VERIFY_INTERFACE_HEADER_SETS") == "ON", (
        '"base" preset must set CMAKE_VERIFY_INTERFACE_HEADER_SETS to "ON"'
    )
    assert base_cache.get("CMAKE_VERIFY_PRIVATE_HEADER_SETS") == "ON", (
        '"base" preset must set CMAKE_VERIFY_PRIVATE_HEADER_SETS to "ON"'
    )

    sysroot_cache = cast(
        "dict[str, object]", configure_by_name["sysroot-release"].get("cacheVariables")
    )
    assert sysroot_cache.get("ENABLE_CLANG_TIDY") == "OFF", (
        '"sysroot-release" must disable clang-tidy for air-gapped SDK hosts'
    )

    for preset_name, preset in configure_by_name.items():
        for section in ("warnings", "errors"):
            block = preset.get(section)
            if isinstance(block, dict):
                assert "dev" not in cast("dict[str, object]", block), (
                    f'Preset {preset_name!r} {section} must not contain removed "dev" key'
                )

    missing_configure = REQUIRED_CONFIGURE_PRESETS - set(configure_by_name)
    assert not missing_configure, (
        f"Missing configurePresets: {sorted(missing_configure)}"
    )

    raw_workflows = data.get("workflowPresets")
    assert isinstance(raw_workflows, list)
    workflow_names: set[str] = set()
    for entry in cast("list[object]", raw_workflows):
        assert isinstance(entry, dict)
        typed_entry = cast("dict[str, object]", entry)
        name = typed_entry.get("name")
        assert isinstance(name, str)
        workflow_names.add(name)
        assert "${configurePresetName}" not in json.dumps(typed_entry), (
            f"workflowPreset {name!r} must not use ${{configurePresetName}}"
        )

    missing_workflows = REQUIRED_WORKFLOW_PRESETS - workflow_names
    assert not missing_workflows, (
        f"Missing workflowPresets: {sorted(missing_workflows)}"
    )


@pytest.mark.unit
def test_cmake_presets_macro_and_test_inheritance_invariants() -> None:
    """Verify non-macro metadata fields and shared testPresets base inheritance."""
    data = _load_json_dict(REPO_ROOT / "CMakePresets.json")
    macro_re = re.compile(r"\$(?:env|penv|vendor)?\{[^}]+\}")

    for array_key in (
        "configurePresets",
        "buildPresets",
        "testPresets",
        "packagePresets",
        "workflowPresets",
    ):
        raw_array = data.get(array_key)
        if not isinstance(raw_array, list):
            continue
        for entry in cast("list[object]", raw_array):
            assert isinstance(entry, dict)
            typed_entry = cast("dict[str, object]", entry)
            preset_name = typed_entry.get("name")
            for text_field in ("displayName", "description"):
                val = typed_entry.get(text_field)
                if isinstance(val, str):
                    assert not macro_re.search(val), (
                        f"{array_key}[{preset_name!r}].{text_field} contains "
                        f"unexpanded macro syntax: {val!r}"
                    )

    raw_tests = data.get("testPresets")
    assert isinstance(raw_tests, list)
    tests_by_name: dict[str, dict[str, object]] = {}
    for entry in cast("list[object]", raw_tests):
        assert isinstance(entry, dict)
        typed_entry = cast("dict[str, object]", entry)
        name = typed_entry.get("name")
        assert isinstance(name, str)
        tests_by_name[name] = typed_entry

    assert "base" in tests_by_name
    test_base = tests_by_name["base"]
    assert test_base.get("hidden") is True
    base_output = cast("dict[str, object]", test_base.get("output"))
    assert "${configurePresetName}" in str(base_output.get("outputJUnitFile"))
    base_exec = cast("dict[str, object]", test_base.get("execution"))
    jobs_val = base_exec.get("jobs")
    assert isinstance(jobs_val, str)
    assert not jobs_val
    assert base_exec.get("noTestsAction") == "error"

    for name, test_preset in tests_by_name.items():
        if test_preset.get("hidden") is True:
            continue
        assert test_preset.get("inherits") == "base", (
            f"testPreset {name!r} must inherit shared 'base' preset"
        )
        exec_block = test_preset.get("execution")
        if isinstance(exec_block, dict):
            assert cast("dict[str, object]", exec_block).get("noTestsAction") != (
                "ignore"
            ), f"testPreset {name!r} must not silence missing tests with 'ignore'"


@pytest.mark.unit
def test_cmake_list_presets_cli() -> None:
    """Verify ``cmake --list-presets=all-defined`` validates CMakePresets.json."""
    cmake_bin = shutil.which("cmake")
    assert cmake_bin is not None, "cmake binary must be available in Pixi environment"

    for flag in ("--list-presets=all", "--list-presets=all-defined"):
        result = subprocess.run(
            [cmake_bin, flag],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        assert "dev-debug" in result.stdout
        assert "ci-debug" in result.stdout
        assert "sysroot-release" in result.stdout


@pytest.mark.unit
def test_gersemirc_configuration() -> None:
    """Verify .gersemirc defines deterministic CMake formatting settings."""
    data = _load_yaml_dict(REPO_ROOT / ".gersemirc")

    assert data.get("indent") == 4
    assert data.get("line_length") == 100
    assert data.get("list_expansion") == "favour-inlining"
    assert data.get("warn_about_unknown_commands") is False


@pytest.mark.unit
def test_cross_toolchains_exist_and_set_cmp0210() -> None:
    """Verify AArch64 and RISC-V cross-compilation toolchains configure CMP0210 NEW."""
    for filename in ("aarch64-linux-gnu.cmake", "riscv64-linux-gnu.cmake"):
        toolchain_path = REPO_ROOT / "cmake" / "toolchains" / filename
        assert toolchain_path.is_file(), f"Missing toolchain file: {toolchain_path}"
        content = toolchain_path.read_text(encoding="utf-8")
        assert re.search(r"cmake_policy\(\s*SET\s+CMP0210\s+NEW\s*\)", content), (
            f"{filename} must set CMP0210 NEW"
        )


@pytest.mark.unit
def test_toolchain_version_pins() -> None:
    """Verify pixi.toml, pyproject.toml, and pre-commit pin upgraded toolchain floors."""
    pixi_data = _load_toml_dict(REPO_ROOT / "pixi.toml")
    pixi_deps = cast("dict[str, object]", pixi_data["dependencies"])
    assert _parse_min_version(str(pixi_deps["cmake"])) >= (4, 5, 0)
    assert _parse_min_version(str(pixi_deps["clang"])) >= (23, 1, 5)
    assert _parse_min_version(str(pixi_deps["ccache"])) >= (4, 14, 3)
    assert _parse_min_version(str(pixi_deps["pyyaml"])) >= (6, 0, 3)

    pixi_pypi = cast("dict[str, object]", pixi_data["pypi-dependencies"])
    assert _parse_min_version(str(pixi_pypi["pytest-asyncio"])) >= (1, 5, 0)

    pyproject_data = _load_toml_dict(REPO_ROOT / "pyproject.toml")
    project_table = cast("dict[str, object]", pyproject_data["project"])
    project_deps = cast("list[object]", project_table["dependencies"])
    assert _parse_min_version(_extract_requirement_spec(project_deps, "pyyaml")) >= (
        6,
        0,
        3,
    )

    dep_groups = cast("dict[str, object]", pyproject_data["dependency-groups"])
    test_group = cast("list[object]", dep_groups["test"])
    assert _parse_min_version(
        _extract_requirement_spec(test_group, "pytest-asyncio")
    ) >= (1, 5, 0)

    precommit_path = REPO_ROOT / ".pre-commit-config.yaml"
    precommit_text = precommit_path.read_text(encoding="utf-8")
    precommit_data = _load_yaml_dict(precommit_path)
    repos = cast("list[object]", precommit_data["repos"])
    clang_format_repos = [
        cast("dict[str, object]", r)
        for r in repos
        if isinstance(r, dict)
        and cast("dict[str, object]", r).get("repo")
        == "https://github.com/pre-commit/mirrors-clang-format"
    ]
    assert len(clang_format_repos) == 1
    assert re.search(
        r"mirrors-clang-format[\s\S]{0,160}#\s*frozen:\s*v23\.1\.5\b",
        precommit_text,
    ), ".pre-commit-config.yaml must pin mirrors-clang-format at v23.1.5"


@pytest.mark.unit
def test_file_set_and_discovery_modernization() -> None:
    """Verify track CMakeLists and CrubitInterop adopt CMake 4.5 FILE_SET and discovery."""
    for rel_path in (
        "src/cpp/leet_code/CMakeLists.txt",
        "src/c/leet_code/CMakeLists.txt",
        "src/cpp/systems/CMakeLists.txt",
        "src/c/systems/CMakeLists.txt",
    ):
        text = (REPO_ROOT / rel_path).read_text(encoding="utf-8")
        assert "FILE_SET HEADERS" in text, f"{rel_path} must use FILE_SET HEADERS"
        assert "FILE_SET sources TYPE SOURCES" in text, (
            f"{rel_path} must use FILE_SET sources TYPE SOURCES"
        )

    crubit_cmake = (REPO_ROOT / "cmake" / "CrubitInterop.cmake").read_text(
        encoding="utf-8"
    )
    assert "FILE_SET generated_sources TYPE SOURCES" in crubit_cmake
    assert "SKIP_LINTING" in crubit_cmake
    assert "crubit_cargo_pool=1" in crubit_cmake
    assert "JOB_POOL crubit_cargo_pool" in crubit_cmake

    c23_test_cmake = (
        REPO_ROOT / "test" / "c" / "systems" / "CMakeLists.txt"
    ).read_text(encoding="utf-8")
    assert "discover_tests(" in c23_test_cmake
    assert "FILE_SET sources TYPE SOURCES" in c23_test_cmake
