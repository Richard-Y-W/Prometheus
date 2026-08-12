"""Non-authoritative historical reference retained until the Program 01B parity review.

No production route may import this module or use it to issue an engineering verdict.
It remains temporarily so the old equations can be compared before deletion.
"""

from dataclasses import dataclass
from math import pi
import random

G = 9.80665

def convert(value: float, source: str, target: str) -> float:
    factors = {("rpm", "rad/s"): 2*pi/60, ("N*mm", "N*m"): .001, ("g", "kg"): .001, ("mm", "m"): .001}
    if source == target: return value
    if (source, target) not in factors: raise ValueError(f"unsupported conversion {source} to {target}")
    return value * factors[(source, target)]

def load_torque(mass: float, radius: float, alpha: float = 0, friction: float = 0) -> float:
    return mass*G*radius + mass*radius*radius*alpha + friction

def motor_torque(load: float, ratio: float, efficiency: float) -> float:
    if ratio <= 0 or not 0 < efficiency <= 1: raise ValueError("invalid drivetrain")
    return load/(ratio*efficiency)

def available_torque(stall: float, speed: float, no_load_speed: float) -> float:
    return max(0.0, stall*(1-speed/no_load_speed))

def motor_current(no_load_current: float, torque: float, torque_constant: float) -> float:
    return no_load_current + torque/torque_constant

def triangular_motion(rotation_deg: float, duration: float) -> tuple[float,float]:
    angle = rotation_deg*pi/180
    return 2*angle/duration, 4*angle/(duration*duration)

def center_of_gravity(points: list[tuple[float, tuple[float,float,float]]]) -> tuple[float,float,float]:
    total=sum(m for m,_ in points)
    if total <= 0: raise ValueError("positive total mass required")
    return tuple(sum(m*p[i] for m,p in points)/total for i in range(3))

def rectangular_tipping_margin(cog: tuple[float,float,float], support: tuple[float,float,float,float], acceleration_xy: tuple[float,float]=(0,0)) -> dict:
    """Signed distance from the effective-gravity projection to a rectangular support boundary.

    Coordinates are metres in a Z-up frame. A positive margin is inside the support
    rectangle; a negative margin is outside. Acceleration is the assembly-frame
    translational acceleration and therefore enters through effective gravity.
    """
    x_min,x_max,y_min,y_max=support
    if not x_min < x_max or not y_min < y_max: raise ValueError("support bounds must have positive area")
    if cog[2] < 0: raise ValueError("center of gravity must not be below the support plane")
    ax,ay=acceleration_xy
    projected=(cog[0]-cog[2]*ax/G,cog[1]-cog[2]*ay/G)
    distances={"x_min":projected[0]-x_min,"x_max":x_max-projected[0],"y_min":projected[1]-y_min,"y_max":y_max-projected[1]}
    edge=min(distances,key=distances.get)
    margin=distances[edge]
    return {"projected_cog_xy_m":projected,"margin_m":margin,"limiting_edge":edge,"stable":margin >= 0,"method":"effective-gravity projection onto rectangular support polygon"}

def thermal_step(temp: float, ambient: float, power: float, resistance: float, capacitance: float, dt: float) -> float:
    return temp + dt*(power-(temp-ambient)/resistance)/capacitance

def thermal_cycle(ambient: float, move_current: float, hold_current: float, winding_resistance: float, r_theta: float, c_theta: float, movement_s: float, hold_s: float, cycle_s: float, cycles: int=120) -> float:
    temp=ambient
    segments=[(movement_s,move_current**2*winding_resistance),(hold_s,hold_current**2*winding_resistance),(cycle_s-movement_s-hold_s,0)]
    for _ in range(cycles):
        for duration,power in segments:
            steps=max(1,int(duration/.1)) if duration else 0
            for _ in range(steps): temp=thermal_step(temp,ambient,power,r_theta,c_theta,duration/steps)
    return temp

def analyze_motor_arm(s: dict, p: dict, samples: int=1000) -> dict:
    speed, alpha = triangular_motion(s["rotation_deg"], s["movement_s"])
    holding_load=load_torque(s["payload_kg"],s["arm_length_m"])
    moving_load=load_torque(s["payload_kg"],s["arm_length_m"],alpha)
    rng=random.Random(496661)
    margins=[]
    for _ in range(samples):
        eff=rng.uniform(*p["gearbox_efficiency_range"])
        required=motor_torque(holding_load,p["gear_ratio"],eff)
        margins.append((p["continuous_torque_nm"]-required)/required)
    margins.sort()
    eff=p["gearbox_efficiency_nominal"]
    hold_motor=motor_torque(holding_load,p["gear_ratio"],eff)
    move_motor=motor_torque(moving_load,p["gear_ratio"],eff)
    motor_speed=speed*p["gear_ratio"]
    available=available_torque(p["stall_torque_nm"],motor_speed,p["no_load_speed_rad_s"])
    hold_current=motor_current(p["no_load_current_a"],hold_motor,p["torque_constant_nm_a"])
    move_current=motor_current(p["no_load_current_a"],move_motor,p["torque_constant_nm_a"])
    peak_temp=thermal_cycle(s["ambient_c"],move_current,hold_current,p["winding_resistance_ohm"],p["thermal_resistance_k_w"],p["thermal_capacitance_j_k"],s["movement_s"],s["hold_s"],s["cycle_s"])
    return {"holding_load_nm":holding_load,"hold_motor_nm":hold_motor,"move_motor_nm":move_motor,"available_move_nm":available,"hold_margin":(p["continuous_torque_nm"]-hold_motor)/hold_motor,"margin_p05":margins[49],"margin_p95":margins[949],"move_current_a":move_current,"hold_current_a":hold_current,"peak_temperature_c":peak_temp,"motor_speed_rad_s":motor_speed}

def severity(margin: float | None, critical_data=True) -> str:
    if not critical_data: return "not_evaluated"
    if margin is None: return "information"
    if margin < 0: return "critical"
    if margin < .2: return "warning"
    return "information"
