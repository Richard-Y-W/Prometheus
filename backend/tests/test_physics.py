from pathlib import Path

import pytest

from app.physics import (
    available_torque,
    center_of_gravity,
    convert,
    load_torque,
    motor_current,
    motor_torque,
    rectangular_tipping_margin,
    severity,
    thermal_cycle,
)
from app.research import choose_claim, model_level
from app.schemas import ScenarioDefinition


APP_DIR = Path(__file__).parents[1] / "app"


def test_reference_only_units_and_motor_math():
    assert convert(60, "rpm", "rad/s") == pytest.approx(6.283185)
    assert convert(1000, "N*mm", "N*m") == 1
    load = load_torque(8, 0.2)
    assert load == pytest.approx(15.69064)
    assert motor_torque(load, 100, 0.7) == pytest.approx(0.224152)
    assert available_torque(2, 50, 100) == 1
    assert motor_current(0.2, 0.1, 0.05) == pytest.approx(2.2)


def test_reference_only_cog_and_thermal():
    assert center_of_gravity([(1, (0, 0, 0)), (1, (2, 0, 0))]) == (1, 0, 0)
    assert thermal_cycle(35, 2, 1, 1, 3, 100, 1, 4, 10, 10) > 35


def test_reference_only_static_and_accelerated_tipping_margin():
    static = rectangular_tipping_margin((0, 0, 0.5), (-0.4, 0.4, -0.3, 0.3))
    assert static["stable"] and static["margin_m"] == pytest.approx(0.3)
    accelerated = rectangular_tipping_margin(
        (0, 0, 0.5), (-0.4, 0.4, -0.3, 0.3), (8, 0)
    )
    assert not accelerated["stable"]
    assert accelerated["limiting_edge"] == "x_min"
    with pytest.raises(ValueError):
        rectangular_tipping_margin((0, 0, 0.5), (0, 0, -1, 1))


def test_reference_only_evidence_precedence_and_conflicts_preserved():
    claims = [
        {"source_authority": "distributor", "normalized_value": "2.6"},
        {"source_authority": "manufacturer", "normalized_value": "3.75"},
    ]
    assert choose_claim(claims)["normalized_value"] == "3.75"
    assert len(claims) == 2


def test_reference_only_model_levels_and_severity():
    assert model_level({"geometry_reference": "x"}) == "geometry"
    assert model_level({"nominal_voltage_v": 36}) == "envelope"
    assert (
        model_level(
            {
                "stall_torque_nm": 1,
                "no_load_speed_rad_s": 2,
                "torque_constant_nm_a": 3,
            }
        )
        == "behavioral"
    )
    assert severity(-0.01) == "critical"
    assert severity(0.1) == "warning"
    assert severity(0.5) == "information"
    assert severity(0.5, False) == "not_evaluated"


def test_reference_only_scenario_validation():
    with pytest.raises(ValueError):
        ScenarioDefinition(
            payload_kg=8,
            arm_length_m=0.2,
            rotation_deg=90,
            movement_s=1.2,
            hold_s=4,
            cycle_s=4,
            ambient_c=35,
        )


def test_reference_only_physics_is_not_imported_by_application_modules():
    forbidden_imports = ("from .physics", "from app.physics", "import app.physics")
    offenders = []
    for path in sorted(APP_DIR.glob("*.py")):
        if path.name == "physics.py":
            continue
        source = path.read_text(encoding="utf-8")
        if any(statement in source for statement in forbidden_imports):
            offenders.append(path.name)
    assert offenders == []
