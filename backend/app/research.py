MOTOR_PARAMETERS = {
    "nominal_voltage_v":36.0,"continuous_torque_nm":0.208,"stall_torque_nm":1.92,
    "torque_constant_nm_a":0.0749,"no_load_speed_rad_s":418.879,"no_load_current_a":0.18,
    "winding_resistance_ohm":1.4,"thermal_resistance_k_w":3.2,"thermal_capacitance_j_k":110.0,
    "maximum_temperature_c":125.0,"gear_ratio":100.0,"gearbox_efficiency_nominal":0.70,
    "gearbox_efficiency_range":[0.55,0.82],"driver_current_limit_a":4.0,"supply_current_limit_a":5.0,
}

UNITS={"nominal_voltage_v":"V","continuous_torque_nm":"N*m","stall_torque_nm":"N*m","torque_constant_nm_a":"N*m/A","no_load_speed_rad_s":"rad/s","no_load_current_a":"A","winding_resistance_ohm":"ohm","thermal_resistance_k_w":"K/W","thermal_capacitance_j_k":"J/K","maximum_temperature_c":"degC","gear_ratio":"1","driver_current_limit_a":"A","supply_current_limit_a":"A"}

def mock_research(manufacturer: str, part_number: str):
    claims=[]
    for field,unit in UNITS.items():
        value=MOTOR_PARAMETERS[field]
        claims.append({"field_name":field,"original_value":str(value),"normalized_value":str(value),"unit":unit,"source_url":"fixture://manufacturer/PM-36-GM","source_document":"PM-36-GM Technical Data","page_or_figure":"Table 1","source_authority":"manufacturer","extraction_status":"validated"})
    return {"manufacturer":manufacturer,"part_number":part_number,"model_level":"behavioral","model_class":"gearmotor","parameters":MOTOR_PARAMETERS,"claims":claims,"missing_fields":["gearbox_efficiency_exact"],"permitted_checks":["torque_speed","current","continuous_hold","thermal_rc","center_of_gravity"],"unsupported_checks":["gearbox_lifetime","drivetrain_vibration"],"pipeline":["identity resolved","authoritative fixture located","specifications read","limits extracted","units validated","no source conflicts","behavioral model built","missing information identified"]}

def choose_claim(claims: list[dict]) -> dict | None:
    rank={"manufacturer":4,"user_measured":3,"trusted_database":2,"distributor":1}
    return max(claims,key=lambda c:rank.get(c["source_authority"],0)) if claims else None

def model_level(parameters: dict) -> str:
    if parameters.get("validated_model_reference"): return "validated"
    if all(k in parameters for k in ("stall_torque_nm","no_load_speed_rad_s","torque_constant_nm_a")): return "behavioral"
    if any(k in parameters for k in ("nominal_voltage_v","mass_kg","load_rating_n")): return "envelope"
    if parameters.get("geometry_reference"): return "geometry"
    return "unsupported"
