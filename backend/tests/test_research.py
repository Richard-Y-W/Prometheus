from app.research import choose_claim, model_level


def test_evidence_precedence_preserves_conflicting_claims():
    claims = [
        {"source_authority": "distributor", "normalized_value": "2.6"},
        {"source_authority": "manufacturer", "normalized_value": "3.75"},
    ]

    assert choose_claim(claims)["normalized_value"] == "3.75"
    assert len(claims) == 2


def test_model_levels_follow_available_evidence():
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
