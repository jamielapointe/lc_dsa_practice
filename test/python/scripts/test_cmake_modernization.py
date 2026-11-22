"""E2E structural and toolchain invariants for CMake 4.5+ and Presets v11."""

import json
import os
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
        r"cmake_diagnostic\(\s*SET\s+CMD_STRICT\s+WARN\s+RECURSE\s*\)",
        content,
    ), "Root CMakeLists.txt must set CMD_STRICT to WARN RECURSE"
    assert re.search(
        r"cmake_diagnostic\(\s*SET\s+CMD_NON_TARGET_DIRECTIVE\s+SEND_ERROR\s*\)",
        content,
    ), "Root CMakeLists.txt must set CMD_NON_TARGET_DIRECTIVE to SEND_ERROR"
    assert re.search(
        r"cmake_diagnostic\(\s*SET\s+CMD_INSTALL_ABSOLUTE_DESTINATION\s+SEND_ERROR\s*\)",
        content,
    ), "Root CMakeLists.txt must set CMD_INSTALL_ABSOLUTE_DESTINATION to SEND_ERROR"
    assert re.search(
        r"cmake_diagnostic\(\s*SET\s+CMD_EXPERIMENTAL\s+SEND_ERROR\s*\)",
        content,
    ), "Root CMakeLists.txt must set CMD_EXPERIMENTAL to SEND_ERROR"
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
def test_cmake_presets_v11_structure() -> None:
    """Verify CMakePresets.json conforms to Schema v11 and exposes required presets."""
    presets_path = REPO_ROOT / "CMakePresets.json"
    raw_text = presets_path.read_text(encoding="utf-8")
    data = _load_json_dict(presets_path)

    assert data.get("version") == 11, "CMakePresets.json must use Schema version 11"
    assert "${configurePresetName}" not in raw_text, (
        "Schema v11 CMakePresets.json must not use v13-only ${configurePresetName}"
    )
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
    assert base_warnings == {
        "dev": True,
        "deprecated": True,
        "uninitialized": True,
        "unusedCli": True,
    }
    assert base_errors == {"deprecated": True}

    base_cache = cast(
        "dict[str, object]", configure_by_name["base"].get("cacheVariables")
    )
    assert base_cache.get("CMAKE_VERIFY_INTERFACE_HEADER_SETS") == "ON", (
        '"base" preset must set CMAKE_VERIFY_INTERFACE_HEADER_SETS to "ON"'
    )
    assert base_cache.get("CMAKE_VERIFY_PRIVATE_HEADER_SETS") == "ON", (
        '"base" preset must set CMAKE_VERIFY_PRIVATE_HEADER_SETS to "ON"'
    )

    for preset_name, preset_cfg in configure_by_name.items():
        cache_vars = cast("dict[str, object]", preset_cfg.get("cacheVariables") or {})
        assert cache_vars.get("ENABLE_CLANG_TIDY") != "OFF", (
            f"Preset {preset_name!r} must not disable mandatory clang-tidy"
        )

    missing_configure = REQUIRED_CONFIGURE_PRESETS - set(configure_by_name)
    assert not missing_configure, (
        f"Missing configurePresets: {sorted(missing_configure)}"
    )

    raw_workflows = data.get("workflowPresets")
    assert isinstance(raw_workflows, list)
    workflow_names = {
        cast("str", cast("dict[str, object]", entry)["name"])
        for entry in cast("list[object]", raw_workflows)
        if isinstance(entry, dict)
        and isinstance(cast("dict[str, object]", entry).get("name"), str)
    }
    missing_workflows = REQUIRED_WORKFLOW_PRESETS - workflow_names
    assert not missing_workflows, (
        f"Missing workflowPresets: {sorted(missing_workflows)}"
    )


@pytest.mark.unit
def test_cmake_presets_v11_diagnostic_keys() -> None:
    """Verify configurePresets warnings/errors strictly use Schema v11 keys."""
    data = _load_json_dict(REPO_ROOT / "CMakePresets.json")
    raw_configure = data.get("configurePresets")
    assert isinstance(raw_configure, list)

    allowed_v11_warnings = frozenset({
        "$comment",
        "dev",
        "deprecated",
        "uninitialized",
        "unusedCli",
        "systemVars",
    })
    allowed_v11_errors = frozenset({"$comment", "dev", "deprecated"})

    for entry in cast("list[object]", raw_configure):
        assert isinstance(entry, dict)
        preset = cast("dict[str, object]", entry)
        preset_name = preset.get("name")
        warnings_block = preset.get("warnings")
        if isinstance(warnings_block, dict):
            unknown_warnings = (
                set(cast("dict[str, object]", warnings_block)) - allowed_v11_warnings
            )
            assert not unknown_warnings, (
                f"Preset {preset_name!r} warnings contains non-v11 keys: "
                f"{sorted(unknown_warnings)}"
            )
        errors_block = preset.get("errors")
        if isinstance(errors_block, dict):
            unknown_errors = (
                set(cast("dict[str, object]", errors_block)) - allowed_v11_errors
            )
            assert not unknown_errors, (
                f"Preset {preset_name!r} errors contains non-v11 keys: "
                f"{sorted(unknown_errors)}"
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
    assert "${presetName}" in str(base_output.get("outputJUnitFile"))
    base_exec = cast("dict[str, object]", test_base.get("execution"))
    assert base_exec.get("jobs") == 4
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


@pytest.mark.unit
def test_clang_tidy_verify_config() -> None:
    """Verify .clang-tidy passes ``clang-tidy --verify-config`` with zero warnings."""
    clang_tidy_bin = shutil.which("clang-tidy")
    assert clang_tidy_bin is not None, "clang-tidy binary must be available in Pixi"

    result = subprocess.run(
        [clang_tidy_bin, "--verify-config"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    combined = f"{result.stdout}\n{result.stderr}"
    assert result.returncode == 0, f"clang-tidy --verify-config failed:\n{combined}"
    assert "warning:" not in combined.lower(), (
        f"clang-tidy --verify-config emitted warnings:\n{combined}"
    )


@pytest.mark.unit
def test_clang_tidy_is_mandatory_in_cmake_and_presets() -> None:
    """Verify StaticAnalysis.cmake requires clang-tidy on every build and preset."""
    static_analysis = (REPO_ROOT / "cmake" / "StaticAnalysis.cmake").read_text(
        encoding="utf-8"
    )
    assert "CMAKE_CROSSCOMPILING" not in static_analysis, (
        "StaticAnalysis.cmake must not skip clang-tidy during cross-compilation"
    )
    assert "option(ENABLE_CLANG_TIDY" not in static_analysis, (
        "StaticAnalysis.cmake must not expose an optional ENABLE_CLANG_TIDY toggle"
    )
    assert "FATAL_ERROR" in static_analysis, (
        "StaticAnalysis.cmake must throw FATAL_ERROR when clang-tidy is missing"
    )
    assert "CMAKE_C_CLANG_TIDY" in static_analysis
    assert "CMAKE_CXX_CLANG_TIDY" in static_analysis

    presets_data = _load_json_dict(REPO_ROOT / "CMakePresets.json")
    raw_configure = cast("list[object]", presets_data.get("configurePresets"))
    for entry in raw_configure:
        preset = cast("dict[str, object]", entry)
        cache_vars = cast("dict[str, object]", preset.get("cacheVariables") or {})
        assert "ENABLE_CLANG_TIDY" not in cache_vars, (
            f"Preset {preset.get('name')!r} must not override ENABLE_CLANG_TIDY"
        )


@pytest.mark.unit
def test_clang_tidy_enables_iwyu_google_and_recommended_checks() -> None:
    """Verify .clang-tidy enables IWYU, Google C++ checks, and recommended rules."""
    clang_tidy_cfg = _load_yaml_dict(REPO_ROOT / ".clang-tidy")
    assert clang_tidy_cfg.get("WarningsAsErrors") == "*"

    raw_checks = str(clang_tidy_cfg.get("Checks", ""))
    checks = {
        part.strip()
        for part in raw_checks.split(",")
        if part.strip() and not part.strip().startswith("-")
    }
    required_checks = {
        "misc-include-cleaner",
        "misc-header-include-cycle",
        "google-build-explicit-make-pair",
        "google-build-namespaces",
        "google-build-using-namespace",
        "google-default-arguments",
        "google-explicit-constructor",
        "google-global-names-in-headers",
        "google-readability-avoid-underscore-in-googletest-name",
        "google-readability-braces-around-statements",
        "google-readability-casting",
        "google-readability-function-size",
        "google-readability-namespace-comments",
        "google-readability-todo",
        "google-runtime-float",
        "google-runtime-int",
        "google-runtime-operator",
        "google-upgrade-googletest-case",
        "concurrency-mt-unsafe",
        "concurrency-thread-canceltype-asynchronous",
        "misc-anonymous-namespace-in-header",
        "bugprone-implicit-widening-of-multiplication-result",
    }
    missing_checks = required_checks - checks
    assert not missing_checks, (
        f".clang-tidy missing required checks: {sorted(missing_checks)}"
    )

    options_list = cast("list[object]", clang_tidy_cfg.get("CheckOptions") or [])
    options_map = {
        str(cast("dict[str, object]", item)["key"]): str(
            cast("dict[str, object]", item)["value"]
        )
        for item in options_list
        if isinstance(item, dict)
    }
    assert options_map.get("misc-include-cleaner.MissingIncludes") == "true"
    assert options_map.get("misc-include-cleaner.UnusedIncludes") == "true"


@pytest.mark.integration
def test_cmake_configure_fails_without_clang_tidy(tmp_path: Path) -> None:
    """Verify CMake configure fails with FATAL_ERROR if clang-tidy is unavailable.

    Args:
        tmp_path: Temporary directory fixture provided by pytest.
    """
    conda_prefix = os.environ.get("CONDA_PREFIX")
    cmake_bin = shutil.which("cmake")
    ninja_bin = shutil.which("ninja")
    if not conda_prefix or cmake_bin is None or ninja_bin is None:
        pytest.skip("Requires active Pixi CONDA_PREFIX, cmake, and ninja")

    conda_bin = Path(conda_prefix) / "bin"
    c_compiler = conda_bin / "clang"
    cxx_compiler = conda_bin / "clang++"
    if not c_compiler.is_file() or not cxx_compiler.is_file():
        pytest.skip("Pixi clang/clang++ compilers unavailable in CONDA_PREFIX")

    missing_tidy_dir = tmp_path / "missing_tidy_build"
    clean_env = dict(os.environ)
    clean_env["PATH"] = "/usr/bin:/bin"
    missing_res = subprocess.run(
        [
            cmake_bin,
            "-S",
            str(REPO_ROOT),
            "-B",
            str(missing_tidy_dir),
            "-G",
            "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja_bin}",
            f"-DCMAKE_C_COMPILER={c_compiler}",
            f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
            f"-DCMAKE_IGNORE_PREFIX_PATH={conda_prefix}",
            f"-DCMAKE_IGNORE_PATH={conda_bin};/opt/homebrew/bin;/opt/homebrew/opt/llvm/bin;/usr/local/bin;/usr/bin",
        ],
        env=clean_env,
        capture_output=True,
        text=True,
        check=False,
    )
    assert missing_res.returncode != 0, (
        "CMake configure must fail when clang-tidy cannot be found"
    )
    assert "clang-tidy executable was not found" in (
        f"{missing_res.stdout}\n{missing_res.stderr}"
    )

    disabled_tidy_dir = tmp_path / "disabled_tidy_build"
    disabled_res = subprocess.run(
        [
            cmake_bin,
            "-S",
            str(REPO_ROOT),
            "-B",
            str(disabled_tidy_dir),
            "-G",
            "Ninja",
            "-DENABLE_CLANG_TIDY=OFF",
            f"-DCMAKE_C_COMPILER={c_compiler}",
            f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
            f"-DCMAKE_PREFIX_PATH={conda_prefix}",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert disabled_res.returncode != 0, (
        "CMake configure must reject -DENABLE_CLANG_TIDY=OFF"
    )
    assert "clang-tidy is mandatory" in (
        f"{disabled_res.stdout}\n{disabled_res.stderr}"
    )


@pytest.mark.unit
def test_cmake_target_hygiene_and_presets_comment() -> None:
    """Verify presets $comment, single header ownership, and systems test labels."""
    presets_data = _load_json_dict(REPO_ROOT / "CMakePresets.json")
    comment = presets_data.get("$comment")
    assert isinstance(comment, list), (
        "CMakePresets.json must define a root '$comment' list"
    )
    assert comment, "CMakePresets.json root '$comment' list must not be empty"
    comment_text = " ".join(str(item) for item in cast("list[object]", comment))
    assert "v11" in comment_text, (
        "CMakePresets.json root '$comment' must document Schema v11 pin"
    )

    leet_cmake = (REPO_ROOT / "src" / "cpp" / "leet_code" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    robot_cleaner_match = re.search(
        r"leet_code_add_library\(\s*robot_room_cleaner\b[\s\S]*?\)",
        leet_cmake,
    )
    assert robot_cleaner_match is not None
    assert "robot.hpp" not in robot_cleaner_match.group(0), (
        "robot_room_cleaner must not duplicate header ownership of robot.hpp"
    )

    systems_test_cmake = (
        REPO_ROOT / "test" / "cpp" / "systems" / "CMakeLists.txt"
    ).read_text(encoding="utf-8")
    fifo_test_match = re.search(
        r"add_project_test\(\s*bounded_fifo_test\b[\s\S]*?\)",
        systems_test_cmake,
    )
    assert fifo_test_match is not None
    assert 'LABELS "systems"' in fifo_test_match.group(0), (
        'bounded_fifo_test must attach LABELS "systems"'
    )


def _verify_installed_cps_and_sbom(install_dir: Path) -> None:
    """Validates installed CPS package metadata and SPDX 3.0.1 SBOM artifact."""
    cps_path = install_dir / "lib" / "cps" / "lc_dsa_practice" / "lc_dsa_practice.cps"
    assert cps_path.is_file(), f"Missing installed CPS descriptor: {cps_path}"
    cps_data = _load_json_dict(cps_path)

    top_requires = cps_data.get("requires")
    if isinstance(top_requires, dict):
        assert "Threads" not in cast("dict[str, object]", top_requires), (
            "Installed CPS must not require non-CPS 'Threads' package"
        )

    components = cast("dict[str, object]", cps_data.get("components"))
    assert "systems_threads" in components
    for comp_name, comp_val in components.items():
        assert isinstance(comp_val, dict)
        comp_dict = cast("dict[str, object]", comp_val)
        assert comp_dict.get("license") == "Apache-2.0", (
            f"CPS component {comp_name!r} missing Apache-2.0 license"
        )

    sbom_dir = install_dir / "lib" / "sbom" / "lc_dsa_practice"
    sbom_files = sorted(sbom_dir.glob("*.spdx.json"))
    assert sbom_files, f"Missing SPDX 3.0.1 SBOM files in {sbom_dir}"
    sbom_data = _load_json_dict(sbom_files[0])
    graph = sbom_data.get("@graph")
    assert isinstance(graph, list)
    assert any(
        isinstance(node, dict)
        and cast("dict[str, object]", node).get("specVersion") == "3.0.1"
        for node in cast("list[object]", graph)
    ), "Installed SBOM must specify SPDX specVersion 3.0.1"


def _build_and_run_cps_consumer(
    cmake_bin: str,
    c_compiler: Path,
    cxx_compiler: Path,
    install_dir: Path,
    tmp_path: Path,
) -> None:
    """Configures, builds, and executes a downstream CMake consumer via CPS."""
    consumer_src = tmp_path / "consumer_src"
    consumer_build = tmp_path / "consumer_build"
    consumer_src.mkdir()
    (consumer_src / "CMakeLists.txt").write_text(
        "\n".join([
            "cmake_minimum_required(VERSION 4.5)",
            "project(cps_consumer LANGUAGES CXX C)",
            "find_package(lc_dsa_practice CONFIG REQUIRED)",
            "add_executable(consumer main.cpp)",
            "target_link_libraries(",
            "    consumer",
            "    PRIVATE",
            "        lc_dsa_practice::systems_bounded_fifo",
            "        lc_dsa_practice::systems_c_bounded_fifo",
            ")",
            "",
        ]),
        encoding="utf-8",
    )
    (consumer_src / "main.cpp").write_text(
        "\n".join([
            "#include <array>",
            "#include <cstdint>",
            "#include <systems/bounded_fifo.h>",
            "#include <systems/bounded_fifo.hpp>",
            "",
            "int main() {",
            "  systems::BoundedFifo<int, 4> cpp_fifo;",
            "  std::array<uint8_t, 16> storage{};",
            "  struct bounded_fifo c_fifo{};",
            "  if (bounded_fifo_init(&c_fifo, storage.data(), 4U, sizeof(int)) !=",
            "      BOUNDED_FIFO_OK) {",
            "    return 1;",
            "  }",
            "  bounded_fifo_destroy(&c_fifo);",
            "  return cpp_fifo.try_push(42) ? 0 : 1;",
            "}",
            "",
        ]),
        encoding="utf-8",
    )
    subprocess.run(
        [
            cmake_bin,
            "-S",
            str(consumer_src),
            "-B",
            str(consumer_build),
            "-G",
            "Ninja",
            f"-DCMAKE_C_COMPILER={c_compiler}",
            f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
            f"-DCMAKE_PREFIX_PATH={install_dir}",
        ],
        check=True,
    )
    subprocess.run([cmake_bin, "--build", str(consumer_build)], check=True)
    subprocess.run([str(consumer_build / "consumer")], check=True)


@pytest.mark.integration
def test_cps_package_install_and_consumer_find_package(tmp_path: Path) -> None:
    """Verify installed CPS package and SPDX 3.0.1 SBOM work in a downstream consumer.

    Args:
        tmp_path: Temporary directory fixture provided by pytest.
    """
    conda_prefix = os.environ.get("CONDA_PREFIX")
    cmake_bin = shutil.which("cmake")
    ninja_bin = shutil.which("ninja")
    if not conda_prefix or cmake_bin is None or ninja_bin is None:
        pytest.skip("Requires active Pixi CONDA_PREFIX, cmake, and ninja")

    conda_bin = Path(conda_prefix) / "bin"
    c_compiler = conda_bin / "clang"
    cxx_compiler = conda_bin / "clang++"
    if not c_compiler.is_file() or not cxx_compiler.is_file():
        pytest.skip("Pixi clang/clang++ compilers unavailable in CONDA_PREFIX")

    build_dir = tmp_path / "pkg_build"
    install_dir = tmp_path / "pkg_install"
    subprocess.run(
        [
            cmake_bin,
            "-S",
            str(REPO_ROOT),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_LEET_CODE=OFF",
            "-DBUILD_SYSTEMS=ON",
            "-DBUILD_TESTING=OFF",
            "-DBUILD_EXAMPLES=OFF",
            f"-DCMAKE_C_COMPILER={c_compiler}",
            f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
            f"-DCMAKE_PREFIX_PATH={conda_prefix}",
            f"-DCMAKE_INSTALL_PREFIX={install_dir}",
        ],
        check=True,
    )
    subprocess.run([cmake_bin, "--build", str(build_dir)], check=True)
    subprocess.run([cmake_bin, "--install", str(build_dir)], check=True)

    _verify_installed_cps_and_sbom(install_dir)
    _build_and_run_cps_consumer(
        cmake_bin, c_compiler, cxx_compiler, install_dir, tmp_path
    )
