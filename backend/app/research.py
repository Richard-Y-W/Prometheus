def choose_claim(claims: list[dict]) -> dict | None:
    rank={"manufacturer":4,"user_measured":3,"trusted_database":2,"distributor":1}
    return max(claims,key=lambda c:rank.get(c["source_authority"],0)) if claims else None

def model_level(parameters: dict) -> str:
    if parameters.get("validated_model_reference"): return "validated"
    if all(k in parameters for k in ("stall_torque_nm","no_load_speed_rad_s","torque_constant_nm_a")): return "behavioral"
    if any(k in parameters for k in ("nominal_voltage_v","mass_kg","load_rating_n")): return "envelope"
    if parameters.get("geometry_reference"): return "geometry"
    return "unsupported"
