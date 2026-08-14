import ast
import re
from pathlib import Path


ROOT = Path(__file__).parents[2]
BACKEND_APP = ROOT / "backend" / "app"


def source_files(directory: Path, suffixes: tuple[str, ...]):
    return (
        path
        for path in sorted(directory.rglob("*"))
        if path.is_file()
        and path.suffix in suffixes
        and "tests" not in path.parts
        and "__pycache__" not in path.parts
    )


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def test_backend_has_no_duplicate_engineering_authority():
    motor_helpers = {
        "load_torque",
        "motor_torque",
        "available_torque",
        "motor_current",
        "triangular_motion",
        "thermal_step",
        "thermal_cycle",
        "analyze_motor_arm",
    }
    offenders = []
    for path in source_files(BACKEND_APP, (".py",)):
        text = path.read_text(encoding="utf-8")
        tree = ast.parse(text, filename=str(path))
        labels = set()
        for node in ast.walk(tree):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                if node.name in motor_helpers:
                    labels.add("motor formula helper")
                if node.name == "severity":
                    labels.add("outcome decision helper")
            elif isinstance(node, ast.Call):
                called_name = (
                    node.func.id
                    if isinstance(node.func, ast.Name)
                    else node.func.attr
                    if isinstance(node.func, ast.Attribute)
                    else None
                )
                if called_name == "Finding":
                    labels.add("authoritative finding construction")
        offenders.extend(f"{relative(path)}: {label}" for label in sorted(labels))

    assert offenders == [], "duplicate authority found:\n" + "\n".join(offenders)


def test_ui_replay_and_store_do_not_embed_pm36_component_values():
    production_roots = (
        ROOT / "desktop" / "app",
        ROOT / "desktop" / "ui",
        ROOT / "desktop" / "replay",
        ROOT / "desktop" / "run_store",
    )
    component_values = re.compile(
        r"(?<![\d.])(?:0\.208|0\.320|1\.92|418\.879|0\.0749)(?![\d.])"
    )
    outcome_decision = re.compile(r"\bhold_margin\b\s*(?:<=|>=|<|>)")
    offenders = []
    for directory in production_roots:
        for path in source_files(directory, (".cpp", ".hpp", ".h", ".qml")):
            text = path.read_text(encoding="utf-8")
            if component_values.search(text):
                offenders.append(f"{relative(path)}: PM-36 value")
            if outcome_decision.search(text):
                offenders.append(f"{relative(path)}: motor outcome decision")

    assert offenders == [], (
        "motor authority embedded in production source: " + ", ".join(offenders)
    )


def test_non_authoritative_layers_have_no_motor_equation_implementation():
    production_roots = (
        BACKEND_APP,
        ROOT / "desktop" / "app",
        ROOT / "desktop" / "ui",
        ROOT / "desktop" / "replay",
        ROOT / "desktop" / "run_store",
    )
    formula_shapes = {
        "gravity load": re.compile(
            r"payload_mass_kg\s*\*\s*(?:standard_gravity|9\.80665)"
        ),
        "drivetrain reduction": re.compile(
            r"gear_ratio\s*\*\s*[\w.]*efficiency|"
            r"(?:load|torque)\s*/\s*\([^)]*gear_ratio"
        ),
        "linear torque-speed": re.compile(
            r"stall_torque_nm\s*\*\s*\(\s*1(?:\.0)?\s*-"
        ),
        "algebraic current": re.compile(
            r"no_load_current_a\s*\+[^;\n]*torque_constant"
        ),
        "thermal evolution": re.compile(
            r"(?:std::)?exp\s*\([^)]*thermal|thermal[^;\n]*\bexp\s*\("
        ),
    }
    offenders = []
    for directory in production_roots:
        for path in source_files(
            directory, (".py", ".cpp", ".hpp", ".h", ".qml")
        ):
            text = path.read_text(encoding="utf-8")
            for label, pattern in formula_shapes.items():
                if pattern.search(text):
                    offenders.append(f"{relative(path)}: {label}")

    assert offenders == [], "duplicate motor equation found:\n" + "\n".join(offenders)


def test_only_execution_adapter_calls_the_authoritative_motor_backend():
    call = re.compile(r"\brun_motor_arm_builtin_v1\s*\(")
    callers = []
    for path in source_files(ROOT / "desktop", (".cpp",)):
        if path == ROOT / "desktop" / "core" / "src" / "motor_arm_builtin_v1.cpp":
            continue
        count = len(call.findall(path.read_text(encoding="utf-8")))
        callers.extend([relative(path)] * count)

    assert callers == ["desktop/execution/src/execute.cpp"]


def test_desktop_and_replay_cli_share_execution_and_replay_libraries():
    desktop_cmake = (ROOT / "desktop" / "app" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    replay_cmake = (ROOT / "desktop" / "replay" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    desktop_links = re.search(
        r"target_link_libraries\(prometheus_desktop_support\s+PUBLIC\s+(.*?)\)",
        desktop_cmake,
        re.DOTALL,
    )
    replay_support_links = re.search(
        r"target_link_libraries\(prometheus_replay_support\s+PUBLIC\s+(.*?)\)",
        replay_cmake,
        re.DOTALL,
    )
    cli_links = re.search(
        r"target_link_libraries\(prometheus_replay\s+PRIVATE\s+(.*?)\)",
        replay_cmake,
        re.DOTALL,
    )

    assert desktop_links is not None
    assert "prometheus_execution" in desktop_links.group(1)
    assert "prometheus_replay_support" in desktop_links.group(1)
    assert replay_support_links is not None
    assert "prometheus_execution" in replay_support_links.group(1)
    assert cli_links is not None
    assert cli_links.group(1).split() == ["prometheus_replay_support"]


def test_production_targets_have_no_test_fallback_or_verification_bypass():
    cmake_files = sorted((ROOT / "desktop").glob("*/CMakeLists.txt"))
    for path in cmake_files:
        production_targets = path.read_text(encoding="utf-8").split(
            "if(BUILD_TESTING)", maxsplit=1
        )[0]
        assert not re.search(
            r"(?:^|[/\\])tests?(?:[/\\]|\.)|\b[\w-]*test[\w-]*\.cpp\b|"
            r"PROMETHEUS_TEST|"
            r"verification_bypass|skip_verification",
            production_targets,
            re.IGNORECASE | re.MULTILINE,
        ), relative(path)

    forbidden_identifiers = re.compile(
        r"\b(?:PROMETHEUS_TEST|verification_bypass|skip_verification|"
        r"disable_verification|trust_unverified)\b",
        re.IGNORECASE,
    )
    offenders = []
    desktop_directories = sorted(
        path for path in (ROOT / "desktop").iterdir() if path.is_dir()
    )
    for directory in desktop_directories:
        for path in source_files(directory, (".cpp", ".hpp", ".h")):
            if forbidden_identifiers.search(path.read_text(encoding="utf-8")):
                offenders.append(relative(path))
    assert offenders == []
