
"""
UAV Endurance Estimator and Optimization Tool
=============================================

Single-file UAV conceptual design tool with:

- Existing endurance estimator workflow
- Six-panel dark dashboard
- Engineering-grade optimization engine
- Optimization dashboard
- Design Explorer for post-optimization what-if analysis
- Feasibility checks and warnings

Only standard Python and matplotlib are required.
No numpy is used in the project logic.
"""

import math
import sys
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.widgets import Slider, Button, CheckButtons


# =============================================================================
# CONSTANTS AND STYLE
# =============================================================================

AIR_DENSITY = 1.225
GRAVITY = 9.81
SAFE_BATTERY_FACTOR = 0.8

BG = "#0d1117"
PANEL = "#161b22"
BORDER = "#30363d"
CYAN = "#58c4dc"
GREEN = "#3fb950"
ORANGE = "#f78166"
PURPLE = "#bc8cff"
YELLOW = "#e3b341"
PINK = "#f778ba"
WHITE = "#c9d1d9"
DIM = "#8b949e"
RED = "#ff7b72"


# =============================================================================
# NUMPY-FREE HELPERS
# =============================================================================

def linspace(start, stop, n):
    """Pure-Python replacement for numpy.linspace."""
    if n <= 1:
        return [start]
    step = (stop - start) / (n - 1)
    return [start + i * step for i in range(n)]


def clamp(value, low, high):
    return max(low, min(high, value))


def fmt(val, digits=2):
    return f"{val:.{digits}f}"


# =============================================================================
# INPUT HELPERS
# =============================================================================

def get_float(prompt, min_val=None, allow_zero=False):
    while True:
        try:
            value = float(input(prompt).strip())
            if min_val is not None:
                if allow_zero:
                    if value < min_val:
                        print(f"  [!] Value must be >= {min_val}. Try again.")
                        continue
                else:
                    if value <= min_val:
                        print(f"  [!] Value must be > {min_val}. Try again.")
                        continue
            return value
        except ValueError:
            print("  [!] Please enter a number.")


def get_int(prompt, min_val=1):
    while True:
        try:
            value = int(input(prompt).strip())
            if value < min_val:
                print(f"  [!] Value must be >= {min_val}. Try again.")
            else:
                return value
        except ValueError:
            print("  [!] Please enter a whole number.")


def get_choice(prompt, choices):
    """Return the lower-case choice among allowed choices."""
    allowed = {c.lower() for c in choices}
    while True:
        value = input(prompt).strip().lower()
        if value in allowed:
            return value
        print(f"  [!] Enter one of: {', '.join(choices)}")


# =============================================================================
# PHYSICS AND ENGINEERING MODELS
# =============================================================================

def battery_energy_wh(voltage_V, capacity_Ah):
    return voltage_V * capacity_Ah


def total_uav_weight(frame_kg, battery_kg, payload_kg, electronics_kg):
    return frame_kg + battery_kg + payload_kg + electronics_kg


def hover_power_physics(total_weight_kg, num_rotors, rotor_diameter_m, efficiency):
    """
    Ideal momentum-theory hover model.

    Engineering note:
    - Larger rotor disk area reduces induced power.
    - More rotors spread thrust, but the total disk area matters most.
    - Efficiency accounts for motor + ESC + drivetrain losses.
    """
    thrust_total = total_weight_kg * GRAVITY
    thrust_per_rotor = thrust_total / num_rotors
    disk_area = math.pi * (rotor_diameter_m / 2.0) ** 2
    if disk_area <= 0 or efficiency <= 0:
        return float("inf")
    power_per_rotor = (thrust_per_rotor ** 1.5) / math.sqrt(2.0 * AIR_DENSITY * disk_area)
    return (power_per_rotor * num_rotors) / efficiency


def endurance_energy_method(voltage_V, capacity_Ah, power_W):
    energy_Wh = battery_energy_wh(voltage_V, capacity_Ah) * SAFE_BATTERY_FACTOR
    if power_W <= 0:
        return float("inf")
    return (energy_Wh / power_W) * 60.0


def endurance_current_method(capacity_Ah, avg_current_A):
    if avg_current_A <= 0:
        return float("inf")
    return (capacity_Ah * 60.0 * SAFE_BATTERY_FACTOR) / avg_current_A


# =============================================================================
# FEASIBILITY AND DESIGN RULES
# =============================================================================

OPT_LIMITS = {
    "voltage_V": (7.4, 44.4),
    "capacity_Ah": (1.0, 30.0),
    "rotor_diameter_m": (0.10, 0.80),
    "num_rotors": (4, 8),
    "motor_efficiency": (0.60, 0.95),
}

# Engineering constraints for dependent mass terms.
# These are not arbitrary UI limits; they encode typical practical bounds.
FRAME_MIN_FACTOR = 0.40      # frame mass should not be reduced below 40% of original
BATTERY_MAX_FACTOR = 2.00    # battery mass may grow with capacity, but not unbounded
ELECTRONICS_MIN_FACTOR = 0.70  # avionics mass can only be reduced slightly without redesign

SUSPICIOUS_ENDURANCE_LOW = 2.0
SUSPICIOUS_ENDURANCE_HIGH = 120.0


def feasibility_check(cfg):
    warnings = []

    # Battery voltage
    if not (OPT_LIMITS["voltage_V"][0] <= cfg["voltage_V"] <= OPT_LIMITS["voltage_V"][1]):
        warnings.append(
            f"Battery voltage {fmt(cfg['voltage_V'], 1)} V is outside the practical range "
            f"{OPT_LIMITS['voltage_V'][0]}–{OPT_LIMITS['voltage_V'][1]} V."
        )

    # Rotor diameter
    if not (OPT_LIMITS["rotor_diameter_m"][0] <= cfg["rotor_diameter_m"] <= OPT_LIMITS["rotor_diameter_m"][1]):
        warnings.append(
            f"Rotor diameter {fmt(cfg['rotor_diameter_m'], 2)} m is outside the practical range "
            f"{OPT_LIMITS['rotor_diameter_m'][0]}–{OPT_LIMITS['rotor_diameter_m'][1]} m."
        )

    # Motor efficiency
    if not (OPT_LIMITS["motor_efficiency"][0] <= cfg["motor_efficiency"] <= OPT_LIMITS["motor_efficiency"][1]):
        warnings.append(
            f"Motor efficiency {fmt(cfg['motor_efficiency'] * 100, 1)}% is outside the normal range "
            f"{OPT_LIMITS['motor_efficiency'][0] * 100:.0f}–{OPT_LIMITS['motor_efficiency'][1] * 100:.0f}%."
        )

    # Weight sanity
    total_w = total_uav_weight(cfg["frame_kg"], cfg["battery_kg"], cfg["payload_kg"], cfg["electronics_kg"])
    if total_w < 0.5:
        warnings.append("Total takeoff weight is very low; the design may be under-modelled or unrealistic.")
    if total_w > 50:
        warnings.append("Total takeoff weight is very high for a small UAV; check structural realism and motor sizing.")

    # Battery and payload plausibility
    if cfg["battery_kg"] <= 0.0:
        warnings.append("Battery mass must be positive.")
    if cfg["payload_kg"] < 0:
        warnings.append("Payload mass cannot be negative.")

    return warnings


def endurance_quality_warning(endurance_min):
    if endurance_min < SUSPICIOUS_ENDURANCE_LOW:
        return "Estimated endurance is suspiciously low. Either payload/weight is excessive or rotor efficiency is poor."
    if endurance_min > SUSPICIOUS_ENDURANCE_HIGH:
        return "Estimated endurance is suspiciously high. Check battery energy, hover power, and units carefully."
    return None


# =============================================================================
# INPUT COLLECTION
# =============================================================================

def collect_inputs():
    sep = "=" * 60
    sep2 = "-" * 60
    print("\n" + sep)
    print("        UAV ENDURANCE ESTIMATOR - INPUT WIZARD")
    print(sep)

    print("\n  STEP 1: UAV COMPONENT WEIGHTS")
    print("  " + sep2)
    frame_kg = get_float("  Frame weight         (kg) : ", min_val=0.0, allow_zero=False)
    battery_kg = get_float("  Battery weight        (kg) : ", min_val=0.0, allow_zero=False)
    payload_kg = get_float("  Payload weight        (kg) : ", min_val=0.0, allow_zero=True)
    electronics_kg = get_float("  Electronics weight    (kg) : ", min_val=0.0, allow_zero=True)

    print("\n  STEP 2: BATTERY PARAMETERS")
    print("  " + sep2)
    print("  Common voltages: 2S=7.4V  |  3S=11.1V  |  4S=14.8V  |  6S=22.2V")
    voltage_V = get_float("  Battery voltage       (V)  : ", min_val=0.0, allow_zero=False)
    capacity_Ah = get_float("  Battery capacity      (Ah) : ", min_val=0.0, allow_zero=False)
    print("  Tip: enter measured average current during hover")
    avg_current_A = get_float("  Average current draw  (A)  : ", min_val=0.0, allow_zero=False)

    print("\n  STEP 3: ROTOR AND MOTOR PARAMETERS")
    print("  " + sep2)
    num_rotors = get_int("  Number of rotors           : ", min_val=1)
    print("  Tip: 10-inch=0.254m  |  12-inch=0.305m  |  15-inch=0.381m")
    rotor_diameter_m = get_float("  Rotor diameter        (m)  : ", min_val=0.0, allow_zero=False)
    print("  Typical range: 0.65 to 0.85  (motor + ESC combined)")
    motor_efficiency = get_float("  Motor efficiency   (0-1)  : ", min_val=0.0, allow_zero=False)
    if motor_efficiency > 1.0:
        print("  [!] Efficiency capped at 1.0")
        motor_efficiency = 1.0

    cfg = dict(
        frame_kg=frame_kg,
        battery_kg=battery_kg,
        payload_kg=payload_kg,
        electronics_kg=electronics_kg,
        voltage_V=voltage_V,
        capacity_Ah=capacity_Ah,
        avg_current_A=avg_current_A,
        num_rotors=num_rotors,
        rotor_diameter_m=rotor_diameter_m,
        motor_efficiency=motor_efficiency,
    )

    warnings = feasibility_check(cfg)
    if warnings:
        print("\n  DESIGN FEASIBILITY WARNINGS")
        print("  " + sep2)
        for w in warnings:
            print("  [!] " + w)
        print("  [i] Keep the warnings in mind; optimization will stay inside realistic bounds.")

    return cfg


# =============================================================================
# CORE EVALUATION
# =============================================================================

def evaluate_design(cfg):
    """
    Compute all core metrics for a design candidate.

    Payload is treated as mission-fixed in optimization logic.
    The optimizer may explore payload only when explicitly unlocked in the explorer.
    """
    total_wt = total_uav_weight(cfg["frame_kg"], cfg["battery_kg"], cfg["payload_kg"], cfg["electronics_kg"])
    energy_Wh = battery_energy_wh(cfg["voltage_V"], cfg["capacity_Ah"])
    hover_pwr = hover_power_physics(total_wt, cfg["num_rotors"], cfg["rotor_diameter_m"], cfg["motor_efficiency"])
    end_physics = endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], hover_pwr)
    end_current = endurance_current_method(cfg["capacity_Ah"], cfg["avg_current_A"])

    base_w = total_uav_weight(cfg["frame_kg"], cfg["battery_kg"], 0.0, cfg["electronics_kg"])
    base_pwr = hover_power_physics(base_w, cfg["num_rotors"], cfg["rotor_diameter_m"], cfg["motor_efficiency"])
    base_end = endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], base_pwr)

    # Payload sensitivity table
    payload_points = []
    for payload in [0.0, 0.5, 1.0, 1.5, 2.0, 3.0]:
        trial_w = base_w + payload
        trial_pwr = hover_power_physics(trial_w, cfg["num_rotors"], cfg["rotor_diameter_m"], cfg["motor_efficiency"])
        trial_end = endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], trial_pwr)
        payload_points.append((payload, trial_end))

    return {
        "total_wt": total_wt,
        "energy_Wh": energy_Wh,
        "hover_pwr": hover_pwr,
        "end_physics": end_physics,
        "end_current": end_current,
        "payload_table": payload_points,
        "base_w": base_w,
        "base_end": base_end,
        "warnings": feasibility_check(cfg),
        "endurance_quality_warning": endurance_quality_warning(end_physics),
    }


def calculate(cfg):
    return evaluate_design(cfg)


# =============================================================================
# REPORTING
# =============================================================================

def print_results(cfg, results):
    sep = "=" * 60
    sep2 = "-" * 60
    w = results["total_wt"]
    en = results["energy_Wh"]
    pw = results["hover_pwr"]
    ep = results["end_physics"]
    ec = results["end_current"]
    pt = results["payload_table"]

    print("\n" + sep)
    print("               UAV ENDURANCE RESULTS")
    print(sep)

    print("\n  WEIGHT BREAKDOWN")
    print("  " + sep2)
    print(f"  Frame               : {cfg['frame_kg']:.3f} kg")
    print(f"  Battery             : {cfg['battery_kg']:.3f} kg")
    print(f"  Payload             : {cfg['payload_kg']:.3f} kg")
    print(f"  Electronics         : {cfg['electronics_kg']:.3f} kg")
    print("  " + sep2)
    print(f"  Total Takeoff Weight: {w:.3f} kg")

    print("\n  BATTERY")
    print("  " + sep2)
    print(f"  Voltage             : {cfg['voltage_V']:.1f} V")
    print(f"  Capacity            : {cfg['capacity_Ah']:.2f} Ah")
    print(f"  Total Energy        : {en:.2f} Wh")
    print(f"  Usable Energy (80%) : {en * SAFE_BATTERY_FACTOR:.2f} Wh")

    print("\n  POWER")
    print("  " + sep2)
    print(f"  Hover Power (calc)  : {pw:.2f} W")
    print(f"  Avg Current (input) : {cfg['avg_current_A']:.1f} A")

    print("\n  ESTIMATED FLIGHT ENDURANCE")
    print("  " + sep2)
    print(f"  Physics-based       : {ep:.1f} min")
    print(f"  Practical (current) : {ec:.1f} min")
    print(f"  Recommended range   : {min(ep, ec):.1f} - {max(ep, ec):.1f} min")

    print("\n  PAYLOAD SENSITIVITY")
    print("  " + sep2)
    print(f"  {'Payload (kg)':<14}  {'Endurance (min)':<22}  Bar")
    print("  " + "-" * 54)
    max_end = max(t for _, t in pt) if pt else 1.0
    for payload, t in pt:
        bar = "#" * max(1, int((t / max_end) * 25))
        mark = " <-- YOUR PAYLOAD" if abs(payload - cfg["payload_kg"]) < 0.01 else ""
        print(f"  {payload:<14.1f}  {t:<22.1f}  {bar}{mark}")

    print("\n  ENGINEERING RECOMMENDATIONS")
    print("  " + sep2)
    recommendations = engineering_recommendations(cfg, results)
    for rec in recommendations:
        print("  - " + rec)
    print("\n" + sep + "\n")


def engineering_recommendations(cfg, results):
    """
    Replace generic tips with engineering recommendations derived from the model.
    """
    recs = []
    ep = results["end_physics"]
    hover = results["hover_pwr"]
    total_w = results["total_wt"]

    # Rank rough drivers using a quick local sensitivity sweep
    ranking = sensitivity_analysis(cfg, results, use_bounds=True, original_cfg=cfg)

    top = ranking[:3]
    if top:
        driver = top[0]["parameter"]
        if driver == "capacity_Ah":
            recs.append("Battery capacity is the strongest endurance limiter; increase capacity only within the mass budget.")
        elif driver == "rotor_diameter_m":
            recs.append("Rotor disk loading is high; a larger propeller would reduce induced power and improve hover efficiency.")
        elif driver == "frame_kg":
            recs.append("Frame mass contributes strongly to takeoff weight; structural lightening has a direct endurance payoff.")
        elif driver == "motor_efficiency":
            recs.append("Motor plus ESC efficiency is a meaningful lever; a higher-efficiency propulsion set gives moderate gain.")
        elif driver == "battery_kg":
            recs.append("Battery mass is constraining endurance; capacity gains should be matched to a realistic mass increase.")
        elif driver == "num_rotors":
            recs.append("Rotor count has an architectural impact; fewer rotors can reduce total propulsion losses if stability allows.")
        elif driver == "electronics_kg":
            recs.append("Electronics mass is not dominant, but trimming avionics mass helps in a payload-limited configuration.")
        elif driver == "voltage_V":
            recs.append("Battery voltage can improve power delivery and enable better motor operating points, but cell count must stay practical.")

    recs.append("Payload is mission-fixed, so optimization should focus on propulsion, energy storage, and structural mass.")
    if ep < 10:
        recs.append("The current design is outside endurance-efficient operating ranges; structural mass and rotor disk area need attention.")
    elif ep < 20:
        recs.append("Endurance is moderate; a combined improvement in battery system and rotor sizing is the most efficient path.")
    else:
        recs.append("Endurance is respectable, but fine-tuning disk loading and mass balance can still produce gains.")
    if hover > 0:
        recs.append(f"Hover power is {hover:.1f} W; reducing disk loading and mass is the most direct way to lower that requirement.")
    if total_w > 0:
        recs.append(f"Current takeoff weight is {total_w:.2f} kg; any mass reduction on non-mission items improves endurance immediately.")
    return recs


# =============================================================================
# SENSITIVITY ANALYSIS
# =============================================================================

def candidate_values_for_parameter(cfg, param, locked_payload=True, original_cfg=None):
    """
    Return a feasible candidate list for each optimizable parameter.

    The search is deterministic, bounded, and engineering-oriented.
    """
    if param == "payload_kg":
        if locked_payload:
            return [cfg["payload_kg"]]
        # unlocked only in the Design Explorer, with a clear warning
        return linspace(max(0.0, cfg["payload_kg"] * 0.5), cfg["payload_kg"] * 1.5 if cfg["payload_kg"] > 0 else 1.0, 7)

    if param == "num_rotors":
        low, high = OPT_LIMITS[param]
        return list(range(low, high + 1))

    current = cfg[param]

    # General 7-point sweep, but with practical clipping for mass terms below.
    ref = original_cfg or cfg
    if param == "frame_kg":
        low = max(ref["frame_kg"] * FRAME_MIN_FACTOR, 0.0)
        high = ref["frame_kg"]  # optimization only reduces frame mass by default
    elif param == "battery_kg":
        low = ref["battery_kg"]
        high = ref["battery_kg"] * BATTERY_MAX_FACTOR
    elif param == "electronics_kg":
        low = max(ref["electronics_kg"] * ELECTRONICS_MIN_FACTOR, 0.0)
        high = ref["electronics_kg"]
    elif param in ("capacity_Ah", "voltage_V", "rotor_diameter_m", "motor_efficiency"):
        low, high = OPT_LIMITS[param]
    else:
        low, high = OPT_LIMITS.get(param, (current, current))

    # Ensure candidate list includes the current value and spans the feasible region.
    if abs(high - low) < 1e-12:
        return [current]

    values = linspace(low, high, 7)
    if current not in values:
        values.append(current)
    # Deduplicate while preserving order.
    out = []
    for v in values:
        if all(abs(v - x) > 1e-12 for x in out):
            out.append(v)
    return out


def sensitivity_analysis(cfg, results=None, use_bounds=False, locked_payload=True, original_cfg=None):
    """
    Rank parameters by estimated endurance leverage.

    - When use_bounds=False: compute local improvement around the current design.
    - When use_bounds=True: use feasible sweep over bounds to estimate leverage.

    This is deterministic and avoids random guessing.
    """
    if results is None:
        results = evaluate_design(cfg)
    if original_cfg is None:
        original_cfg = cfg

    base_end = results["end_physics"]
    sensitivity_rows = []

    for param in ["capacity_Ah", "rotor_diameter_m", "frame_kg", "battery_kg", "motor_efficiency", "num_rotors", "electronics_kg", "voltage_V"]:
        candidates = candidate_values_for_parameter(cfg, param, locked_payload=locked_payload, original_cfg=original_cfg)
        best_end = base_end
        best_val = cfg[param]

        # One-variable sweep while keeping the rest fixed.
        for cand in candidates:
            trial = dict(cfg)
            trial[param] = cand
            trial_end = evaluate_design(trial)["end_physics"]
            if trial_end > best_end:
                best_end = trial_end
                best_val = cand

        gain = best_end - base_end
        sensitivity_rows.append({
            "parameter": param,
            "current": cfg[param],
            "best": best_val,
            "gain": gain,
            "best_end": best_end,
        })

    sensitivity_rows.sort(key=lambda x: x["gain"], reverse=True)
    return sensitivity_rows


# =============================================================================
# OPTIMIZATION ENGINE
# =============================================================================

def parameter_description(param):
    return {
        "voltage_V": "Battery voltage",
        "capacity_Ah": "Battery capacity",
        "rotor_diameter_m": "Rotor diameter",
        "num_rotors": "Number of rotors",
        "motor_efficiency": "Motor efficiency",
        "frame_kg": "Frame weight",
        "battery_kg": "Battery weight",
        "electronics_kg": "Electronics weight",
        "payload_kg": "Payload weight",
    }.get(param, param)


def locked_reason(param):
    return {
        "payload_kg": "Payload is mission-critical and fixed unless the mission itself changes.",
        "num_rotors": "Rotor count is constrained by airframe architecture and control authority.",
        "frame_kg": "Frame mass can only be reduced within structural integrity limits.",
        "battery_kg": "Battery mass can only increase within practical packaging and CG limits.",
        "electronics_kg": "Electronics mass is only slightly reducible without redesigning avionics.",
    }.get(param, "")


def optimize_design(cfg, verbose=False):
    """
    Level-2 engineering optimization:
    1) Sensitivity ranking
    2) Deterministic bounded parameter sweep
    3) Greedy improvement with feasibility constraints

    Payload remains fixed unless explicitly unlocked.
    """
    current = dict(cfg)
    current_results = evaluate_design(current)

    # Local search state
    history = []
    locked_params = {"payload_kg"}  # mission-fixed by default
    # Additional partial locks are expressed through bounds, not hard locks.
    candidate_params = ["capacity_Ah", "rotor_diameter_m", "frame_kg", "battery_kg",
                        "motor_efficiency", "num_rotors", "electronics_kg", "voltage_V"]

    while True:
        ranking = sensitivity_analysis(current, evaluate_design(current), use_bounds=True, locked_payload=True, original_cfg=cfg)
        improved = False
        best_step = None
        best_trial = None
        base_end = evaluate_design(current)["end_physics"]

        # Greedy search: try the most promising parameters first.
        for row in ranking:
            param = row["parameter"]
            if param in locked_params:
                continue

            candidates = candidate_values_for_parameter(current, param, locked_payload=True, original_cfg=cfg)
            best_local_end = base_end
            best_local_val = current[param]
            best_local_trial = None

            for cand in candidates:
                if abs(cand - current[param]) < 1e-12:
                    continue

                trial = dict(current)
                trial[param] = cand

                # Additional engineering constraints on dependent parameters.
                trial = enforce_limits(trial, cfg)
                trial_results = evaluate_design(trial)
                trial_end = trial_results["end_physics"]

                if trial_end > best_local_end + 1e-9:
                    best_local_end = trial_end
                    best_local_val = cand
                    best_local_trial = trial

            if best_local_trial is not None:
                gain = best_local_end - base_end
                if best_step is None or gain > best_step["gain"]:
                    best_step = {
                        "parameter": param,
                        "from": current[param],
                        "to": best_local_val,
                        "gain": gain,
                        "before_end": base_end,
                        "after_end": best_local_end,
                    }
                    best_trial = best_local_trial

        if best_step is not None and best_step["gain"] > 1e-6:
            history.append(best_step)
            current = best_trial
            improved = True
        if not improved:
            break

        # Hard stop if optimization converges to a near-flat result.
        if len(history) > 20:
            break

    optimized = current
    opt_results = evaluate_design(optimized)

    # Summarize locked parameters and final values
    final_changes = compare_designs(cfg, optimized, optimized)
    return {
        "current_cfg": cfg,
        "optimized_cfg": optimized,
        "current_results": current_results,
        "optimized_results": opt_results,
        "history": history,
        "locked_params": locked_params,
        "final_changes": final_changes,
        "ranking": sensitivity_analysis(cfg, current_results, use_bounds=True, locked_payload=True),
    }


def enforce_limits(trial, original_cfg):
    """
    Apply feasibility limits for optimization candidates.

    Engineering rationale:
    - Payload is fixed by mission requirement.
    - Rotor count stays within practical multirotor architecture limits.
    - Frame mass can only be reduced to avoid underestimating structural mass.
    - Battery mass can only increase to reflect the energy-mass tradeoff.
    - Electronics mass can only decrease slightly; major savings imply a redesign.
    """
    trial = dict(trial)

    # Clamp the direct design variables
    trial["voltage_V"] = clamp(trial["voltage_V"], *OPT_LIMITS["voltage_V"])
    trial["capacity_Ah"] = clamp(trial["capacity_Ah"], *OPT_LIMITS["capacity_Ah"])
    trial["rotor_diameter_m"] = clamp(trial["rotor_diameter_m"], *OPT_LIMITS["rotor_diameter_m"])
    trial["motor_efficiency"] = clamp(trial["motor_efficiency"], *OPT_LIMITS["motor_efficiency"])

    # Integer rotor count
    trial["num_rotors"] = int(round(clamp(trial["num_rotors"], *OPT_LIMITS["num_rotors"])))

    # Mass constraints relative to original design
    trial["frame_kg"] = clamp(trial["frame_kg"], original_cfg["frame_kg"] * FRAME_MIN_FACTOR, original_cfg["frame_kg"])
    trial["battery_kg"] = clamp(trial["battery_kg"], original_cfg["battery_kg"], original_cfg["battery_kg"] * BATTERY_MAX_FACTOR)
    trial["electronics_kg"] = clamp(trial["electronics_kg"], original_cfg["electronics_kg"] * ELECTRONICS_MIN_FACTOR, original_cfg["electronics_kg"])

    # Payload fixed unless a user explicitly unlocks it in the explorer
    trial["payload_kg"] = original_cfg["payload_kg"]

    return trial


def compare_designs(original_cfg, optimized_cfg, user_cfg, user_mode=False):
    """
    Build a parameter-by-parameter comparison dictionary.
    """
    keys = ["voltage_V", "capacity_Ah", "rotor_diameter_m", "num_rotors",
            "motor_efficiency", "frame_kg", "battery_kg", "electronics_kg", "payload_kg"]

    compare = {}
    for k in keys:
        compare[k] = {
            "original": original_cfg[k],
            "optimized": optimized_cfg[k],
            "user": user_cfg[k],
        }

    # Endurance and weight metrics
    o_res = evaluate_design(original_cfg)
    z_res = evaluate_design(optimized_cfg)
    u_res = evaluate_design(user_cfg)
    compare["endurance_min"] = {
        "original": o_res["end_physics"],
        "optimized": z_res["end_physics"],
        "user": u_res["end_physics"],
    }
    compare["hover_power_W"] = {
        "original": o_res["hover_pwr"],
        "optimized": z_res["hover_pwr"],
        "user": u_res["hover_pwr"],
    }
    compare["weight_kg"] = {
        "original": o_res["total_wt"],
        "optimized": z_res["total_wt"],
        "user": u_res["total_wt"],
    }
    return compare


def format_report_value(param, value):
    if param == "num_rotors":
        return f"{int(round(value))}"
    if param == "motor_efficiency":
        return f"{value:.3f}"
    if param in ("voltage_V",):
        return f"{value:.1f} V"
    if param in ("capacity_Ah",):
        return f"{value:.2f} Ah"
    if param in ("rotor_diameter_m",):
        return f"{value:.2f} m"
    return f"{value:.2f} kg"


def print_optimization_report(opt):
    current_cfg = opt["current_cfg"]
    optimized_cfg = opt["optimized_cfg"]
    current_results = opt["current_results"]
    optimized_results = opt["optimized_results"]
    history = opt["history"]

    sep = "=" * 60
    sep2 = "-" * 60
    curr_end = current_results["end_physics"]
    opt_end = optimized_results["end_physics"]
    gain = opt_end - curr_end
    gain_pct = (gain / curr_end * 100.0) if curr_end > 0 else 0.0

    print("\n" + sep)
    print("                 OPTIMIZATION RESULTS")
    print(sep)
    print(f"\nCurrent Endurance: {curr_end:.1f} min\n")
    print("Optimization Results:\n")

    if history:
        for step in history:
            p = step["parameter"]
            print(f"Change {parameter_description(p)}:")
            print(f"{format_report_value(p, step['from'])} → {format_report_value(p, step['to'])}")
            print(f"Gain: +{step['gain']:.1f} min\n")
    else:
        print("No feasible improvement found within the engineering bounds.\n")

    print(f"Optimized Endurance:\n{opt_end:.1f} min")
    print(f"Improvement:\n+{gain:.1f} min (+{gain_pct:.1f}%)\n")

    print("Final Optimized Parameters:")
    print(sep2)
    for k in ["voltage_V", "capacity_Ah", "rotor_diameter_m", "num_rotors", "motor_efficiency",
              "frame_kg", "battery_kg", "electronics_kg", "payload_kg"]:
        changed = abs(optimized_cfg[k] - current_cfg[k]) > 1e-9
        lock_note = ""
        if k == "payload_kg":
            lock_note = " [LOCKED]"
        print(f"  {parameter_description(k):<18}: {format_report_value(k, optimized_cfg[k])}{lock_note}")
        if changed and k != "payload_kg":
            print(f"    from {format_report_value(k, current_cfg[k])}")

    print("\nLocked Parameters:")
    print(sep2)
    print("  Payload weight is fixed by mission requirement and was not altered by the optimizer.")
    print("  Additional constraints were enforced through practical bounds on frame, battery, and electronics mass.")
    if history:
        print("\nWhy these parameters were chosen:")
        for step in history[:5]:
            print(f"  - {parameter_description(step['parameter'])}: largest endurance gain under the current constraint set.")
        print("\nTrade-offs:")
        print("  - Larger energy storage typically increases mass, cost, and packaging difficulty.")
        print("  - Larger rotors can improve hover efficiency but increase footprint and structural sensitivity.")
        print("  - Lower frame mass is beneficial only if stiffness, strength, and landing loads remain acceptable.")
    print("\n" + sep + "\n")


# =============================================================================
# ORIGINAL DASHBOARD
# =============================================================================

def _theme(ax):
    ax.set_facecolor(PANEL)
    ax.tick_params(colors=DIM, labelsize=9)
    ax.xaxis.label.set_color(WHITE)
    ax.yaxis.label.set_color(WHITE)
    ax.title.set_color(WHITE)
    for s in ax.spines.values():
        s.set_edgecolor(BORDER)
    ax.grid(True, color=BORDER, linestyle="--", linewidth=0.6, alpha=0.7)


def show_graphs(cfg, results):
    plt.rcParams.update({
        "figure.facecolor": BG,
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "text.color": WHITE,
        "axes.labelcolor": WHITE,
        "xtick.color": DIM,
        "ytick.color": DIM,
    })

    fig = plt.figure(figsize=(18, 11), facecolor=BG)
    fig.suptitle("UAV Endurance Dashboard", fontsize=18, fontweight="bold", color=WHITE, y=0.98)

    gs = gridspec.GridSpec(2, 3, figure=fig, hspace=0.45, wspace=0.32,
                           left=0.06, right=0.97, top=0.92, bottom=0.07)

    ax1 = fig.add_subplot(gs[0, 0])
    ax2 = fig.add_subplot(gs[0, 1])
    ax3 = fig.add_subplot(gs[0, 2])
    ax4 = fig.add_subplot(gs[1, 0])
    ax5 = fig.add_subplot(gs[1, 1])
    ax6 = fig.add_subplot(gs[1, 2])

    base_w = cfg["frame_kg"] + cfg["battery_kg"] + cfg["electronics_kg"]
    total_w = results["total_wt"]
    pwr_tot = results["hover_pwr"]
    ep = results["end_physics"]
    N = 200

    # GRAPH 1: Payload vs Endurance
    pls = linspace(0, 5, N)
    ends = []
    for p in pls:
        total = base_w + p
        pwr = hover_power_physics(total, cfg["num_rotors"], cfg["rotor_diameter_m"], cfg["motor_efficiency"])
        ends.append(endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], pwr))
    ax1.plot(pls, ends, color=CYAN, lw=2.2, zorder=3)
    ax1.fill_between(pls, ends, alpha=0.13, color=CYAN)
    ax1.axvline(cfg["payload_kg"], color=YELLOW, lw=1.6, ls="--", label="Your payload")
    ax1.axhline(ep, color=GREEN, lw=1.4, ls=":", label="Your endurance")
    ax1.scatter([cfg["payload_kg"]], [ep], color=YELLOW, s=70, zorder=6)
    ax1.annotate(f"{ep:.1f} min", xy=(cfg["payload_kg"], ep), xytext=(8, 6),
                 textcoords="offset points", color=YELLOW, fontsize=8.5)
    ax1.set_xlabel("Payload (kg)")
    ax1.set_ylabel("Endurance (min)")
    ax1.set_title("Payload vs Endurance")
    ax1.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax1)

    # GRAPH 2: Battery Voltage vs Endurance
    volts = linspace(7.4, 44.4, N)
    v_ends = [endurance_energy_method(v, cfg["capacity_Ah"], pwr_tot) for v in volts]
    ax2.plot(volts, v_ends, color=ORANGE, lw=2.2, zorder=3)
    ax2.fill_between(volts, v_ends, alpha=0.13, color=ORANGE)
    for vv, lbl in [(7.4, "2S"), (11.1, "3S"), (14.8, "4S"), (22.2, "6S"), (29.6, "8S"), (44.4, "12S")]:
        ax2.axvline(vv, color=BORDER, lw=1, ls=":")
        ax2.text(vv + 0.3, min(v_ends) + 0.5, lbl, color=DIM, fontsize=8)
    ax2.axvline(cfg["voltage_V"], color=YELLOW, lw=1.6, ls="--", label="Your voltage")
    ax2.scatter([cfg["voltage_V"]], [ep], color=YELLOW, s=70, zorder=6)
    ax2.set_xlabel("Battery Voltage (V)")
    ax2.set_ylabel("Endurance (min)")
    ax2.set_title("Battery Voltage vs Endurance")
    ax2.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax2)

    # GRAPH 3: Battery Capacity vs Endurance
    caps = linspace(1, 30, N)
    c_ends = [endurance_energy_method(cfg["voltage_V"], c, pwr_tot) for c in caps]
    ax3.plot(caps, c_ends, color=PURPLE, lw=2.2, zorder=3)
    ax3.fill_between(caps, c_ends, alpha=0.13, color=PURPLE)
    ax3.axvline(cfg["capacity_Ah"], color=YELLOW, lw=1.6, ls="--", label="Your capacity")
    ax3.scatter([cfg["capacity_Ah"]], [ep], color=YELLOW, s=70, zorder=6)
    ax3.annotate(f"{ep:.1f} min", xy=(cfg["capacity_Ah"], ep), xytext=(8, -14),
                 textcoords="offset points", color=YELLOW, fontsize=8.5)
    ax3.set_xlabel("Battery Capacity (Ah)")
    ax3.set_ylabel("Endurance (min)")
    ax3.set_title("Battery Capacity vs Endurance")
    ax3.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax3)

    # GRAPH 4: Weight Breakdown Donut
    raw_sizes = [cfg["frame_kg"], cfg["battery_kg"], cfg["payload_kg"], cfg["electronics_kg"]]
    raw_labels = [
        f"Frame\n{cfg['frame_kg']:.2f} kg",
        f"Battery\n{cfg['battery_kg']:.2f} kg",
        f"Payload\n{cfg['payload_kg']:.2f} kg",
        f"Electronics\n{cfg['electronics_kg']:.2f} kg",
    ]
    raw_colors = [CYAN, GREEN, ORANGE, PURPLE]
    filtered = [(s, l, c) for s, l, c in zip(raw_sizes, raw_labels, raw_colors) if s > 0]
    nz_sizes = [x[0] for x in filtered]
    nz_labels = [x[1] for x in filtered]
    nz_colors = [x[2] for x in filtered]
    ax4.pie(
        nz_sizes, labels=nz_labels, colors=nz_colors,
        explode=[0.03] * len(nz_sizes),
        autopct="%1.1f%%", startangle=110,
        wedgeprops=dict(width=0.52, edgecolor=BG, linewidth=2),
        textprops=dict(color=WHITE, fontsize=8.5),
    )
    ax4.set_facecolor(PANEL)
    ax4.set_title(f"Weight Breakdown  ({total_w:.2f} kg total)")
    ax4.title.set_color(WHITE)

    # GRAPH 5: Rotor Diameter vs Endurance
    dias = linspace(0.10, 0.80, N)
    d_ends = []
    for dia in dias:
        pwr = hover_power_physics(total_w, cfg["num_rotors"], dia, cfg["motor_efficiency"])
        d_ends.append(endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], pwr))
    dias_cm = [d * 100 for d in dias]
    ax5.plot(dias_cm, d_ends, color=PINK, lw=2.2, zorder=3)
    ax5.fill_between(dias_cm, d_ends, alpha=0.13, color=PINK)
    ax5.axvline(cfg["rotor_diameter_m"] * 100, color=YELLOW, lw=1.6, ls="--", label="Your rotor")
    ax5.scatter([cfg["rotor_diameter_m"] * 100], [ep], color=YELLOW, s=70, zorder=6)
    for inch, lbl in [(10, '10"'), (12, '12"'), (15, '15"')]:
        ref = inch * 0.0254 * 100
        ax5.axvline(ref, color=BORDER, lw=1, ls=":")
        ax5.text(ref + 0.3, min(d_ends) + 0.5, lbl, color=DIM, fontsize=8)
    ax5.set_xlabel("Rotor Diameter (cm)")
    ax5.set_ylabel("Endurance (min)")
    ax5.set_title("Rotor Diameter vs Endurance")
    ax5.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax5)

    # GRAPH 6: Motor Efficiency vs Endurance
    etas = linspace(0.30, 0.98, N)
    e_ends = []
    for eta in etas:
        pwr = hover_power_physics(total_w, cfg["num_rotors"], cfg["rotor_diameter_m"], eta)
        e_ends.append(endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], pwr))
    etas_pct = [e * 100 for e in etas]
    ax6.plot(etas_pct, e_ends, color=GREEN, lw=2.2, zorder=3)
    ax6.fill_between(etas_pct, e_ends, alpha=0.13, color=GREEN)
    ax6.axvline(cfg["motor_efficiency"] * 100, color=YELLOW, lw=1.6, ls="--", label="Your efficiency")
    ax6.scatter([cfg["motor_efficiency"] * 100], [ep], color=YELLOW, s=70, zorder=6)
    ax6.annotate(f"{ep:.1f} min", xy=(cfg["motor_efficiency"] * 100, ep), xytext=(6, 6),
                 textcoords="offset points", color=YELLOW, fontsize=8.5)
    ax6.set_xlabel("Motor Efficiency (%)")
    ax6.set_ylabel("Endurance (min)")
    ax6.set_title("Motor Efficiency vs Endurance")
    ax6.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax6)

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.show()


# =============================================================================
# OPTIMIZATION DASHBOARD
# =============================================================================

def card_text(ax, title, value, subtitle="", value_color=WHITE):
    ax.set_facecolor(PANEL)
    ax.set_xticks([])
    ax.set_yticks([])
    for s in ax.spines.values():
        s.set_edgecolor(BORDER)
    ax.text(0.03, 0.85, title, color=DIM, fontsize=10, weight="bold", transform=ax.transAxes)
    ax.text(0.03, 0.45, value, color=value_color, fontsize=18, weight="bold", transform=ax.transAxes)
    if subtitle:
        ax.text(0.03, 0.12, subtitle, color=WHITE, fontsize=9, transform=ax.transAxes)


def show_optimization_dashboard(opt):
    curr_cfg = opt["current_cfg"]
    opt_cfg = opt["optimized_cfg"]
    curr_res = opt["current_results"]
    opt_res = opt["optimized_results"]
    history = opt["history"]
    ranking = opt["ranking"]

    plt.rcParams.update({
        "figure.facecolor": BG,
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "text.color": WHITE,
        "axes.labelcolor": WHITE,
        "xtick.color": DIM,
        "ytick.color": DIM,
    })

    fig = plt.figure(figsize=(19, 12), facecolor=BG)
    fig.suptitle("UAV Optimization Dashboard", fontsize=18, fontweight="bold", color=WHITE, y=0.985)

    gs = gridspec.GridSpec(4, 2, figure=fig, hspace=0.42, wspace=0.24,
                           left=0.05, right=0.98, top=0.93, bottom=0.06)

    ax1 = fig.add_subplot(gs[0, 0])
    ax2 = fig.add_subplot(gs[0, 1])
    ax3 = fig.add_subplot(gs[1, 0])
    ax4 = fig.add_subplot(gs[1, 1])
    ax5 = fig.add_subplot(gs[2, 0])
    ax6 = fig.add_subplot(gs[2, 1])
    ax7 = fig.add_subplot(gs[3, 0])
    ax8 = fig.add_subplot(gs[3, 1])

    curr_end = curr_res["end_physics"]
    opt_end = opt_res["end_physics"]
    gain = opt_end - curr_end
    gain_pct = (gain / curr_end * 100.0) if curr_end > 0 else 0.0

    card_text(ax1, "Current Endurance", f"{curr_end:.1f} min",
              f"Hover power: {curr_res['hover_pwr']:.1f} W")
    card_text(ax2, "Optimized Endurance", f"{opt_end:.1f} min",
              f"Hover power: {opt_res['hover_pwr']:.1f} W", value_color=GREEN)
    card_text(ax3, "Improvement", f"+{gain_pct:.1f}%",
              f"Absolute gain: +{gain:.1f} min", value_color=YELLOW)

    # Sensitivity ranking chart
    ax4.set_facecolor(PANEL)
    top = ranking[:6]
    names = [parameter_description(r["parameter"]) for r in top][::-1]
    gains = [r["gain"] for r in top][::-1]
    colors = [CYAN, ORANGE, PURPLE, GREEN, PINK, YELLOW][::-1]
    ax4.barh(names, gains, color=colors)
    ax4.set_title("Sensitivity Ranking", color=WHITE)
    ax4.set_xlabel("Endurance gain if moved to best feasible value")
    _theme(ax4)

    # Current vs optimized comparison chart
    ax5.set_facecolor(PANEL)
    labels = ["Endurance (min)", "Hover Power (W)", "Takeoff Weight (kg)"]
    curr_vals = [curr_end, curr_res["hover_pwr"], curr_res["total_wt"]]
    opt_vals = [opt_end, opt_res["hover_pwr"], opt_res["total_wt"]]
    x = list(range(len(labels)))
    width = 0.36
    ax5.bar([i - width / 2 for i in x], curr_vals, width=width, label="Current", color=ORANGE)
    ax5.bar([i + width / 2 for i in x], opt_vals, width=width, label="Optimized", color=GREEN)
    ax5.set_xticks(x)
    ax5.set_xticklabels(labels, rotation=0)
    ax5.set_title("Current vs Optimized Comparison", color=WHITE)
    ax5.legend(facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax5)

    # Parameter contribution bar chart
    ax6.set_facecolor(PANEL)
    contrib_names = []
    contrib_vals = []
    for step in history:
        contrib_names.append(parameter_description(step["parameter"]))
        contrib_vals.append(step["gain"])
    if not contrib_names:
        contrib_names = ["No change"]
        contrib_vals = [0.0]
    ax6.barh(contrib_names[::-1], contrib_vals[::-1], color=CYAN)
    ax6.set_title("Parameter Contribution", color=WHITE)
    ax6.set_xlabel("Gain added in optimization sequence")
    _theme(ax6)

    # Before vs after weight breakdown
    ax7.set_facecolor(PANEL)
    orig_weights = [curr_cfg["frame_kg"], curr_cfg["battery_kg"], curr_cfg["payload_kg"], curr_cfg["electronics_kg"]]
    opt_weights = [opt_cfg["frame_kg"], opt_cfg["battery_kg"], opt_cfg["payload_kg"], opt_cfg["electronics_kg"]]
    parts = ["Frame", "Battery", "Payload", "Electronics"]
    x = list(range(len(parts)))
    width = 0.36
    ax7.bar([i - width / 2 for i in x], orig_weights, width=width, label="Current", color=PINK)
    ax7.bar([i + width / 2 for i in x], opt_weights, width=width, label="Optimized", color=GREEN)
    ax7.set_xticks(x)
    ax7.set_xticklabels(parts)
    ax7.set_title("Before vs After Weight Breakdown", color=WHITE)
    ax7.legend(facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax7)

    # Optimization summary box
    ax8.set_facecolor(PANEL)
    ax8.set_xticks([])
    ax8.set_yticks([])
    for s in ax8.spines.values():
        s.set_edgecolor(BORDER)
    summary_lines = [
        f"Current endurance: {curr_end:.1f} min",
        f"Optimized endurance: {opt_end:.1f} min",
        f"Improvement: +{gain:.1f} min ({gain_pct:.1f}%)",
        "",
        "Changed parameters:",
    ]
    if history:
        for step in history[:5]:
            summary_lines.append(
                f"- {parameter_description(step['parameter'])}: {format_report_value(step['from'])} → {format_report_value(step['to'])}"
            )
    else:
        summary_lines.append("- No feasible changes found.")
    summary_lines.extend([
        "",
        "Trade-off note:",
        "Improving endurance often increases cost, size, or packaging complexity.",
        "Payload stayed fixed; the optimizer focused on propulsion,",
        "energy storage, and structural mass within realistic bounds.",
    ])
    text = "\n".join(summary_lines)
    ax8.text(0.03, 0.95, "Optimization Summary", color=DIM, fontsize=11, weight="bold", va="top", transform=ax8.transAxes)
    ax8.text(0.03, 0.86, text, color=WHITE, fontsize=10, va="top", transform=ax8.transAxes)
    ax8.text(
        0.03, 0.08,
        "Locked: payload weight by default. Structural and battery bounds enforced.",
        color=YELLOW, fontsize=9, transform=ax8.transAxes
    )

    plt.show()


# =============================================================================
# DESIGN EXPLORER
# =============================================================================

def update_user_modified_design(state):
    """
    Recompute user-modified design in-place.
    State is a dict so the slider callbacks can mutate it.
    """
    cfg = dict(state["base_opt_cfg"])

    cfg["voltage_V"] = state["sliders"]["voltage_V"].val
    cfg["capacity_Ah"] = state["sliders"]["capacity_Ah"].val
    cfg["rotor_diameter_m"] = state["sliders"]["rotor_diameter_m"].val
    cfg["num_rotors"] = int(round(state["sliders"]["num_rotors"].val))
    cfg["motor_efficiency"] = state["sliders"]["motor_efficiency"].val
    cfg["frame_kg"] = state["sliders"]["frame_kg"].val
    cfg["battery_kg"] = state["sliders"]["battery_kg"].val
    cfg["electronics_kg"] = state["sliders"]["electronics_kg"].val

    if state["payload_unlocked"]:
        cfg["payload_kg"] = state["sliders"]["payload_kg"].val
    else:
        cfg["payload_kg"] = state["base_opt_cfg"]["payload_kg"]

    cfg = enforce_limits(cfg, state["original_cfg"])
    state["user_cfg"] = cfg
    state["user_results"] = evaluate_design(cfg)
    return cfg, state["user_results"]


def set_slider_ranges(state, base_cfg, original_cfg):
    # Set feasibility-constrained ranges for the explorer.
    s = state["sliders"]

    s["voltage_V"].set_val(base_cfg["voltage_V"])
    s["capacity_Ah"].set_val(base_cfg["capacity_Ah"])
    s["rotor_diameter_m"].set_val(base_cfg["rotor_diameter_m"])
    s["num_rotors"].set_val(base_cfg["num_rotors"])
    s["motor_efficiency"].set_val(base_cfg["motor_efficiency"])
    s["frame_kg"].set_val(base_cfg["frame_kg"])
    s["battery_kg"].set_val(base_cfg["battery_kg"])
    s["electronics_kg"].set_val(base_cfg["electronics_kg"])
    s["payload_kg"].set_val(base_cfg["payload_kg"])


def show_design_explorer(opt):
    """
    Interactive post-optimization what-if analysis.

    The optimizer's recommendation is loaded as the default design.
    The user can override values and immediately see the effect.
    """
    original_cfg = opt["current_cfg"]
    optimized_cfg = opt["optimized_cfg"]

    plt.rcParams.update({
        "figure.facecolor": BG,
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "text.color": WHITE,
        "axes.labelcolor": WHITE,
        "xtick.color": DIM,
        "ytick.color": DIM,
    })

    fig = plt.figure(figsize=(19, 12), facecolor=BG)
    fig.suptitle("Design Explorer", fontsize=18, fontweight="bold", color=WHITE, y=0.985)

    # Main analysis axes
    gs = gridspec.GridSpec(3, 2, figure=fig, hspace=0.35, wspace=0.25,
                           left=0.33, right=0.97, top=0.92, bottom=0.08)
    ax_cmp = fig.add_subplot(gs[0, :])
    ax_wt = fig.add_subplot(gs[1, 0])
    ax_txt = fig.add_subplot(gs[1, 1])
    ax_cmp_tbl = fig.add_subplot(gs[2, :])

    # Slider area
    slider_left = 0.05
    slider_width = 0.22
    slider_h = 0.022
    slider_gap = 0.036

    slider_specs = [
        ("voltage_V", "Battery Voltage", 7.4, 44.4, optimized_cfg["voltage_V"]),
        ("capacity_Ah", "Battery Capacity", 1.0, 30.0, optimized_cfg["capacity_Ah"]),
        ("rotor_diameter_m", "Rotor Diameter", 0.10, 0.80, optimized_cfg["rotor_diameter_m"]),
        ("num_rotors", "Number of Rotors", 4, 8, optimized_cfg["num_rotors"]),
        ("motor_efficiency", "Motor Efficiency", 0.60, 0.95, optimized_cfg["motor_efficiency"]),
        ("frame_kg", "Frame Weight", original_cfg["frame_kg"] * FRAME_MIN_FACTOR, original_cfg["frame_kg"], optimized_cfg["frame_kg"]),
        ("battery_kg", "Battery Weight", optimized_cfg["battery_kg"], original_cfg["battery_kg"] * BATTERY_MAX_FACTOR, optimized_cfg["battery_kg"]),
        ("electronics_kg", "Electronics Weight", original_cfg["electronics_kg"] * ELECTRONICS_MIN_FACTOR, original_cfg["electronics_kg"], optimized_cfg["electronics_kg"]),
        ("payload_kg", "Payload Weight", 0.0, max(1.0, original_cfg["payload_kg"] * 1.5), optimized_cfg["payload_kg"]),
    ]

    sliders = {}
    slider_axes = {}

    for i, (key, label, vmin, vmax, vinit) in enumerate(slider_specs):
        y = 0.88 - i * slider_gap
        ax_s = fig.add_axes([slider_left, y, slider_width, slider_h], facecolor=PANEL)
        slider_axes[key] = ax_s
        is_int = key == "num_rotors"
        if is_int:
            slider = Slider(ax_s, label, vmin, vmax, valinit=vinit, valstep=1, color=CYAN)
        else:
            slider = Slider(ax_s, label, vmin, vmax, valinit=vinit, color=CYAN)
        sliders[key] = slider

    # Payload lock warning / toggle
    ax_cb = fig.add_axes([0.05, 0.10, 0.22, 0.06], facecolor=PANEL)
    check = CheckButtons(ax_cb, ["Unlock payload"], [False])
    for line in check.lines:
        for l in line:
            l.set_color(WHITE)

    # Status text
    ax_status = fig.add_axes([0.05, 0.02, 0.22, 0.06], facecolor=PANEL)
    ax_status.set_xticks([])
    ax_status.set_yticks([])
    for s in ax_status.spines.values():
        s.set_edgecolor(BORDER)
    status_text = ax_status.text(0.03, 0.5, "", color=WHITE, va="center", transform=ax_status.transAxes)

    # Comparison plotting
    def render():
        state["payload_unlocked"] = check.get_status()[0]

        user_cfg, user_res = update_user_modified_design(state)
        curr_res = opt["current_results"]
        opt_res = opt["optimized_results"]

        # Update summary text
        unlock_state = "UNLOCKED" if state["payload_unlocked"] else "LOCKED"
        payload_note = (
            "Payload is locked by default.\n"
            "Unlocking it triggers a warning because payload is mission-fixed."
        )
        status_text.set_text(f"Payload: {unlock_state}\n{payload_note}")

        # Comparison bar chart
        ax_cmp.clear()
        ax_cmp.set_facecolor(PANEL)
        labels = ["Endurance (min)", "Hover Power (W)", "Takeoff Weight (kg)"]
        original_vals = [curr_res["end_physics"], curr_res["hover_pwr"], curr_res["total_wt"]]
        optimized_vals = [opt_res["end_physics"], opt_res["hover_pwr"], opt_res["total_wt"]]
        user_vals = [user_res["end_physics"], user_res["hover_pwr"], user_res["total_wt"]]
        x = list(range(len(labels)))
        width = 0.24
        ax_cmp.bar([i - width for i in x], original_vals, width=width, label="Original", color=ORANGE)
        ax_cmp.bar(x, optimized_vals, width=width, label="Optimized", color=GREEN)
        ax_cmp.bar([i + width for i in x], user_vals, width=width, label="User Modified", color=CYAN)
        ax_cmp.set_xticks(x)
        ax_cmp.set_xticklabels(labels)
        ax_cmp.set_title("Current / Optimized / User-Modified Comparison", color=WHITE)
        ax_cmp.legend(facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
        _theme(ax_cmp)

        # Weight breakdown
        ax_wt.clear()
        ax_wt.set_facecolor(PANEL)
        parts = ["Frame", "Battery", "Payload", "Electronics"]
        orig = [original_cfg["frame_kg"], original_cfg["battery_kg"], original_cfg["payload_kg"], original_cfg["electronics_kg"]]
        optw = [optimized_cfg["frame_kg"], optimized_cfg["battery_kg"], optimized_cfg["payload_kg"], optimized_cfg["electronics_kg"]]
        userw = [user_cfg["frame_kg"], user_cfg["battery_kg"], user_cfg["payload_kg"], user_cfg["electronics_kg"]]
        x = list(range(len(parts)))
        ax_wt.bar([i - 0.25 for i in x], orig, width=0.24, color=PINK, label="Original")
        ax_wt.bar(x, optw, width=0.24, color=GREEN, label="Optimized")
        ax_wt.bar([i + 0.25 for i in x], userw, width=0.24, color=CYAN, label="User")
        ax_wt.set_xticks(x)
        ax_wt.set_xticklabels(parts)
        ax_wt.set_title("Weight Breakdown", color=WHITE)
        ax_wt.legend(facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
        _theme(ax_wt)

        # Engineering recommendations box
        ax_txt.clear()
        ax_txt.set_facecolor(PANEL)
        ax_txt.set_xticks([])
        ax_txt.set_yticks([])
        for s in ax_txt.spines.values():
            s.set_edgecolor(BORDER)

        recs = engineering_recommendations(user_cfg, user_res)
        warning = user_res["endurance_quality_warning"]
        lines = [
            f"Current endurance: {curr_res['end_physics']:.1f} min",
            f"Optimized endurance: {opt_res['end_physics']:.1f} min",
            f"User-modified endurance: {user_res['end_physics']:.1f} min",
            "",
            "Engineering recommendations:",
        ]
        for r in recs[:5]:
            lines.append(f"- {r}")
        if warning:
            lines.extend(["", "Warning:", warning])

        ax_txt.text(0.03, 0.95, "\n".join(lines), color=WHITE, fontsize=9.5, va="top", transform=ax_txt.transAxes)

        # Comparison table
        ax_cmp_tbl.clear()
        ax_cmp_tbl.set_facecolor(PANEL)
        ax_cmp_tbl.axis("off")
        rows = [
            ("Battery Voltage", f"{original_cfg['voltage_V']:.1f} V", f"{optimized_cfg['voltage_V']:.1f} V", f"{user_cfg['voltage_V']:.1f} V"),
            ("Battery Capacity", f"{original_cfg['capacity_Ah']:.2f} Ah", f"{optimized_cfg['capacity_Ah']:.2f} Ah", f"{user_cfg['capacity_Ah']:.2f} Ah"),
            ("Rotor Diameter", f"{original_cfg['rotor_diameter_m']:.2f} m", f"{optimized_cfg['rotor_diameter_m']:.2f} m", f"{user_cfg['rotor_diameter_m']:.2f} m"),
            ("Number of Rotors", f"{int(round(original_cfg['num_rotors']))}", f"{int(round(optimized_cfg['num_rotors']))}", f"{int(round(user_cfg['num_rotors']))}"),
            ("Frame Weight", f"{original_cfg['frame_kg']:.2f} kg", f"{optimized_cfg['frame_kg']:.2f} kg", f"{user_cfg['frame_kg']:.2f} kg"),
            ("Battery Weight", f"{original_cfg['battery_kg']:.2f} kg", f"{optimized_cfg['battery_kg']:.2f} kg", f"{user_cfg['battery_kg']:.2f} kg"),
            ("Electronics Weight", f"{original_cfg['electronics_kg']:.2f} kg", f"{optimized_cfg['electronics_kg']:.2f} kg", f"{user_cfg['electronics_kg']:.2f} kg"),
            ("Payload", f"{original_cfg['payload_kg']:.2f} kg", f"{optimized_cfg['payload_kg']:.2f} kg", f"{user_cfg['payload_kg']:.2f} kg"),
            ("Endurance", f"{curr_res['end_physics']:.1f} min", f"{opt_res['end_physics']:.1f} min", f"{user_res['end_physics']:.1f} min"),
        ]
        table = ax_cmp_tbl.table(
            cellText=[list(r) for r in rows],
            colLabels=["Parameter", "Original", "Optimized", "User Modified"],
            loc="center",
            cellLoc="center",
            colLoc="center",
        )
        table.auto_set_font_size(False)
        table.set_fontsize(9)
        table.scale(1, 1.35)
        for (r, c), cell in table.get_celld().items():
            cell.set_edgecolor(BORDER)
            cell.set_facecolor(PANEL)
            cell.get_text().set_color(WHITE)
            if r == 0:
                cell.get_text().set_weight("bold")
                cell.set_facecolor("#1f2937")

        fig.canvas.draw_idle()

    state = {
        "original_cfg": original_cfg,
        "base_opt_cfg": dict(optimized_cfg),
        "sliders": sliders,
        "payload_unlocked": False,
        "user_cfg": dict(optimized_cfg),
        "user_results": evaluate_design(dict(optimized_cfg)),
    }

    # Register callbacks
    def on_slider_change(val):
        render()

    for s in sliders.values():
        s.on_changed(on_slider_change)

    def on_unlock(label):
        # Payload unlock is allowed only with a clear warning.
        render()

    check.on_clicked(on_unlock)

    # Reset and close buttons
    ax_reset = fig.add_axes([0.05, 0.17, 0.10, 0.035], facecolor=PANEL)
    btn_reset = Button(ax_reset, "Reset", color=BORDER, hovercolor="#4b5563")
    ax_close = fig.add_axes([0.17, 0.17, 0.10, 0.035], facecolor=PANEL)
    btn_close = Button(ax_close, "Close", color=BORDER, hovercolor="#4b5563")

    def reset(event):
        for k, slider in sliders.items():
            slider.set_val(optimized_cfg[k])
        if check.get_status()[0]:
            check.set_active(0)
        render()

    def close(event):
        plt.close(fig)

    btn_reset.on_clicked(reset)
    btn_close.on_clicked(close)

    # Set initial display
    render()
    plt.show()


# =============================================================================
# MAIN WORKFLOW
# =============================================================================

def run_design_cycle():
    cfg = collect_inputs()
    results = calculate(cfg)
    print_results(cfg, results)

    print("  Opening original dashboard window... (close it to continue)\n")
    show_graphs(cfg, results)

    choice = get_choice("  Optimize the design now? (o = optimize, r = rerun, q = quit) : ", ["o", "r", "q"])
    if choice == "q":
        print("\n  Goodbye!\n")
        return None
    if choice == "r":
        return "rerun"

    opt = optimize_design(cfg)
    print_optimization_report(opt)

    print("  Opening optimization dashboard window... (close it to continue)\n")
    show_optimization_dashboard(opt)

    print("  Opening Design Explorer... adjust sliders to compare the designs.\n")
    show_design_explorer(opt)

    return None


def main():
    try:
        while True:
            result = run_design_cycle()
            if result != "rerun":
                break
    except KeyboardInterrupt:
        print("\n\n  [Interrupted] Goodbye!\n")
        sys.exit(0)
    except ZeroDivisionError:
        print("\n  [ERROR] Division by zero. Check power/current values.")
        sys.exit(1)
    except Exception as e:
        print(f"\n  [ERROR] {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
