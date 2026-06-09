"""
UAV Endurance Estimator & Optimization Tool
=========================================================
Requires ONLY matplotlib (no numpy needed).

Features:
  - Base endurance and hover power estimation
  - Physics-based & current-based endurance methods
  - Engineering-grade constrained optimization module
  - Parameter sensitivity analysis
  - Original and Optimization visual dashboards
  - Interactive "What-If" Post-Optimization Design Explorer

Install:
    pip install matplotlib

Run:
    python uav_estimator_with_graphs.py
"""

import math
import sys
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

# ── Constants ─────────────────────────────────────────────────────────────────
AIR_DENSITY         = 1.225
GRAVITY             = 9.81
SAFE_BATTERY_FACTOR = 0.8

# ── Colour palette ────────────────────────────────────────────────────────────
BG     = "#0d1117"
PANEL  = "#161b22"
BORDER = "#30363d"
CYAN   = "#58c4dc"
GREEN  = "#3fb950"
ORANGE = "#f78166"
PURPLE = "#bc8cff"
YELLOW = "#e3b341"
PINK   = "#f778ba"
WHITE  = "#c9d1d9"
DIM    = "#8b949e"


# =============================================================================
# NUMPY-FREE HELPERS
# =============================================================================

def linspace(start, stop, n):
    """Pure-Python replacement for numpy.linspace."""
    if n <= 1:
        return [start]
    step = (stop - start) / (n - 1)
    return [start + i * step for i in range(n)]

def copy_cfg(cfg):
    """Deep copy for the configuration dictionary."""
    return {k: v for k, v in cfg.items()}


# =============================================================================
# INPUT HELPERS
# =============================================================================

def get_float(prompt, min_val=0.0, allow_zero=False):
    while True:
        try:
            value = float(input(prompt).strip())
            if allow_zero and value < min_val:
                print("  [!] Value must be >= {}. Try again.".format(min_val))
            elif not allow_zero and value <= min_val:
                print("  [!] Value must be > {}. Try again.".format(min_val))
            else:
                return value
        except ValueError:
            print("  [!] Please enter a number.")

def get_int(prompt, min_val=1):
    while True:
        try:
            value = int(input(prompt).strip())
            if value < min_val:
                print("  [!] Value must be >= {}. Try again.".format(min_val))
            else:
                return value
        except ValueError:
            print("  [!] Please enter a whole number.")


# =============================================================================
# PHYSICS FUNCTIONS
# =============================================================================

def battery_energy(voltage_V, capacity_Ah):
    return voltage_V * capacity_Ah

def total_uav_weight(frame_kg, battery_kg, payload_kg, electronics_kg):
    return frame_kg + battery_kg + payload_kg + electronics_kg

def hover_power_physics(total_weight_kg, num_rotors, rotor_diameter_m, efficiency):
    thrust_total     = total_weight_kg * GRAVITY
    thrust_per_rotor = thrust_total / num_rotors
    disk_area        = math.pi * (rotor_diameter_m / 2.0) ** 2
    power_per_rotor  = (thrust_per_rotor ** 1.5) / math.sqrt(2.0 * AIR_DENSITY * disk_area)
    return (power_per_rotor * num_rotors) / efficiency

def endurance_energy_method(voltage_V, capacity_Ah, power_W):
    energy_Wh = battery_energy(voltage_V, capacity_Ah) * SAFE_BATTERY_FACTOR
    return (energy_Wh / power_W) * 60.0

def endurance_current_method(capacity_Ah, avg_current_A):
    return (capacity_Ah * 60.0 * SAFE_BATTERY_FACTOR) / avg_current_A

def evaluate_design(cfg):
    """Wrapper to quickly return estimated physics-based endurance."""
    w_total = total_uav_weight(cfg["frame_kg"], cfg["battery_kg"], cfg["payload_kg"], cfg["electronics_kg"])
    pwr = hover_power_physics(w_total, cfg["num_rotors"], cfg["rotor_diameter_m"], cfg["motor_efficiency"])
    return endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], pwr)


# =============================================================================
# INPUT COLLECTION
# =============================================================================

def collect_inputs():
    SEP  = "=" * 55
    SEP2 = "-" * 55
    print("\n" + SEP)
    print("        UAV ENDURANCE ESTIMATOR - INPUT WIZARD")
    print(SEP)

    print("\n  STEP 1: UAV COMPONENT WEIGHTS")
    print("  " + SEP2)
    frame_kg       = get_float("  Frame weight         (kg) : ", allow_zero=False)
    battery_kg     = get_float("  Battery weight       (kg) : ", allow_zero=False)
    payload_kg     = get_float("  Payload weight       (kg) : ", allow_zero=True)
    electronics_kg = get_float("  Electronics weight   (kg) : ", allow_zero=True)

    print("\n  STEP 2: BATTERY PARAMETERS")
    print("  " + SEP2)
    print("  Common voltages: 3S=11.1V  |  4S=14.8V  |  6S=22.2V")
    voltage_V     = get_float("  Battery voltage      (V)  : ", allow_zero=False)
    capacity_Ah   = get_float("  Battery capacity     (Ah) : ", allow_zero=False)
    avg_current_A = get_float("  Avg current draw     (A)  : ", allow_zero=False)

    print("\n  STEP 3: ROTOR AND MOTOR PARAMETERS")
    print("  " + SEP2)
    num_rotors = get_int("  Number of rotors          : ", min_val=1)
    print("  Tip: 10-inch=0.254m  |  12-inch=0.305m  |  15-inch=0.381m")
    rotor_diameter_m = get_float("  Rotor diameter       (m)  : ", allow_zero=False)
    motor_efficiency = get_float("  Motor efficiency   (0-1)  : ", allow_zero=False)
    if motor_efficiency > 1.0:
        print("  [!] Efficiency capped at 1.0")
        motor_efficiency = 1.0

    return dict(
        frame_kg=frame_kg, battery_kg=battery_kg,
        payload_kg=payload_kg, electronics_kg=electronics_kg,
        voltage_V=voltage_V, capacity_Ah=capacity_Ah,
        avg_current_A=avg_current_A, num_rotors=num_rotors,
        rotor_diameter_m=rotor_diameter_m, motor_efficiency=motor_efficiency,
    )


# =============================================================================
# CALCULATE ALL RESULTS
# =============================================================================

def calculate(cfg):
    w_total   = total_uav_weight(cfg["frame_kg"], cfg["battery_kg"],
                                  cfg["payload_kg"], cfg["electronics_kg"])
    energy_Wh = battery_energy(cfg["voltage_V"], cfg["capacity_Ah"])
    pwr       = hover_power_physics(w_total, cfg["num_rotors"],
                                     cfg["rotor_diameter_m"], cfg["motor_efficiency"])
    ep        = endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], pwr)
    ec        = endurance_current_method(cfg["capacity_Ah"], cfg["avg_current_A"])

    base_w   = total_uav_weight(cfg["frame_kg"], cfg["battery_kg"], 0.0, cfg["electronics_kg"])
    base_pwr = hover_power_physics(base_w, cfg["num_rotors"],
                                    cfg["rotor_diameter_m"], cfg["motor_efficiency"])
    base_end = endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"], base_pwr)
    pt = []
    for p in [0.0, 0.5, 1.0, 1.5, 2.0]:
        ratio = ((base_w + p) / base_w) ** 1.5
        pt.append((p, base_end / ratio))

    return dict(
        total_wt=w_total, energy_Wh=energy_Wh, hover_pwr=pwr,
        end_physics=ep, end_current=ec, payload_table=pt,
    )


# =============================================================================
# ENGINEERING RECOMMENDATIONS & FEASIBILITY
# =============================================================================

def print_engineering_recommendations(cfg, results):
    SEP2 = "-" * 55
    print("\n  ENGINEERING RECOMMENDATIONS")
    print("  " + SEP2)
    ep = results["end_physics"]
    w_total = results["total_wt"]
    
    disk_area = cfg["num_rotors"] * math.pi * (cfg["rotor_diameter_m"] / 2.0)**2
    disk_loading = (w_total * GRAVITY) / disk_area

    if ep < 15:
        print("  [!] LOW ENDURANCE: The current configuration limits flight time.")
    if cfg["frame_kg"] / w_total > 0.40:
        print("  [*] MASS: Frame mass is >40% of takeoff weight. Consider lighter materials (e.g., carbon fiber).")
    if disk_loading > 100:
        print("  [*] PROPULSION: Rotor disk loading is high (>100 N/m²). A larger propeller would significantly improve hover efficiency.")
    if cfg["motor_efficiency"] < 0.75:
        print("  [*] POWERTRAIN: Motor/ESC efficiency is low. High-quality propulsion components can easily add 1-3 mins of flight time.")
    print("  [*] PAYLOAD: Payload is fixed. Optimization should focus on propulsion and battery energy density.")
    print("\n" + "=" * 55 + "\n")

def check_feasibility(cfg):
    """Checks the design against realistic physical limits and returns warnings."""
    warnings = []
    if cfg['voltage_V'] < 7.4 or cfg['voltage_V'] > 44.4:
        warnings.append(f"Voltage ({cfg['voltage_V']:.1f}V) is outside typical 2S-12S UAV range.")
    if cfg['rotor_diameter_m'] < 0.1 or cfg['rotor_diameter_m'] > 1.0:
        warnings.append(f"Rotor diameter ({cfg['rotor_diameter_m']:.2f}m) is unusually small/large.")
    if cfg['motor_efficiency'] > 0.95:
        warnings.append("Motor efficiency > 95% is physically unlikely for standard UAVs.")
    
    energy_density = (cfg['voltage_V'] * cfg['capacity_Ah']) / (cfg['battery_kg'] + 1e-6)
    if energy_density > 260:
        warnings.append(f"Unrealistic battery energy density ({energy_density:.0f} Wh/kg). Limit is ~250 Wh/kg.")
    
    w_total = total_uav_weight(cfg["frame_kg"], cfg["battery_kg"], cfg["payload_kg"], cfg["electronics_kg"])
    disk_area = cfg['num_rotors'] * math.pi * (cfg['rotor_diameter_m'] / 2.0)**2
    disk_loading = (w_total * GRAVITY) / disk_area
    if disk_loading > 150:
        warnings.append(f"Dangerous rotor disk loading ({disk_loading:.0f} N/m²). Hover efficiency/stability will fail.")
        
    return warnings


# =============================================================================
# OPTIMIZATION ENGINE (LEVEL 2 ENGINEERING)
# =============================================================================

def optimize_design(orig_cfg):
    """
    Performs multi-variable engineering search using Coordinate Ascent.
    Uses sequential parameter improvement respecting feasibility constraints.
    """
    cfg = copy_cfg(orig_cfg)
    current_endurance = evaluate_design(cfg)
    base_endurance = current_endurance
    history = []

    # Maximum 20 optimization steps to prevent infinite loops
    for _ in range(20):
        best_gain = 0
        best_param = None
        best_test_cfg = None

        # 1. Optimize Battery Capacity & Weight
        # Reason: Capacity increases must linearly scale battery weight to maintain realistic energy density.
        test_cfg = copy_cfg(cfg)
        test_cfg['capacity_Ah'] *= 1.05
        test_cfg['battery_kg'] *= 1.05 
        if test_cfg['capacity_Ah'] <= 30.0 and test_cfg['battery_kg'] <= orig_cfg['battery_kg'] * 2.0:
            e = evaluate_design(test_cfg)
            gain = e - current_endurance
            if gain > best_gain + 0.05:
                best_gain = gain
                best_param = 'Battery Capacity & Weight'
                best_test_cfg = test_cfg

        # 2. Optimize Rotor Diameter
        # Reason: Larger rotors are more efficient in hover, bounded by frame clearance limits (+30%).
        test_cfg = copy_cfg(cfg)
        test_cfg['rotor_diameter_m'] *= 1.05
        if test_cfg['rotor_diameter_m'] <= orig_cfg['rotor_diameter_m'] * 1.3 and test_cfg['rotor_diameter_m'] <= 0.80:
            e = evaluate_design(test_cfg)
            gain = e - current_endurance
            if gain > best_gain + 0.05:
                best_gain = gain
                best_param = 'Rotor Diameter'
                best_test_cfg = test_cfg

        # 3. Optimize Frame Weight
        # Reason: Structural integrity limits how much mass can be shaved off (-60% limit via composites).
        test_cfg = copy_cfg(cfg)
        test_cfg['frame_kg'] *= 0.95
        if test_cfg['frame_kg'] >= orig_cfg['frame_kg'] * 0.40:
            e = evaluate_design(test_cfg)
            gain = e - current_endurance
            if gain > best_gain + 0.05:
                best_gain = gain
                best_param = 'Frame Weight'
                best_test_cfg = test_cfg

        # 4. Optimize Motor/ESC Efficiency
        # Reason: Component limits restrict efficiency to ~90% practically.
        test_cfg = copy_cfg(cfg)
        test_cfg['motor_efficiency'] += 0.02
        if test_cfg['motor_efficiency'] <= 0.90:
            e = evaluate_design(test_cfg)
            gain = e - current_endurance
            if gain > best_gain + 0.05:
                best_gain = gain
                best_param = 'Motor Efficiency'
                best_test_cfg = test_cfg

        # Apply the best theoretical move if one exists
        if best_param:
            history.append((best_param, copy_cfg(cfg), copy_cfg(best_test_cfg), best_gain))
            cfg = copy_cfg(best_test_cfg)
            current_endurance += best_gain
        else:
            break

    # Aggregate step-by-step history into a final summary report
    summary = {}
    for param, old_c, new_c, gain in history:
        if param not in summary:
            if param == 'Battery Capacity & Weight':
                summary[param] = {'old_val': f"{old_c['capacity_Ah']:.1f} Ah", 'new_val': f"{new_c['capacity_Ah']:.1f} Ah", 'gain': gain}
            elif param == 'Rotor Diameter':
                summary[param] = {'old_val': f"{old_c['rotor_diameter_m']:.2f} m", 'new_val': f"{new_c['rotor_diameter_m']:.2f} m", 'gain': gain}
            elif param == 'Frame Weight':
                summary[param] = {'old_val': f"{old_c['frame_kg']:.2f} kg", 'new_val': f"{new_c['frame_kg']:.2f} kg", 'gain': gain}
            elif param == 'Motor Efficiency':
                summary[param] = {'old_val': f"{old_c['motor_efficiency']:.2f}", 'new_val': f"{new_c['motor_efficiency']:.2f}", 'gain': gain}
        else:
            if param == 'Battery Capacity & Weight':
                summary[param]['new_val'] = f"{new_c['capacity_Ah']:.1f} Ah"
            elif param == 'Rotor Diameter':
                summary[param]['new_val'] = f"{new_c['rotor_diameter_m']:.2f} m"
            elif param == 'Frame Weight':
                summary[param]['new_val'] = f"{new_c['frame_kg']:.2f} kg"
            elif param == 'Motor Efficiency':
                summary[param]['new_val'] = f"{new_c['motor_efficiency']:.2f}"
            summary[param]['gain'] += gain

    return cfg, summary, base_endurance, current_endurance

def calculate_sensitivities(cfg):
    """Calculates partial derivative (sensitivity) of endurance to a 1% change in variables."""
    base_end = evaluate_design(cfg)
    sens = {}
    params = ['capacity_Ah', 'rotor_diameter_m', 'motor_efficiency', 'frame_kg', 'battery_kg', 'voltage_V']
    for p in params:
        test_cfg = copy_cfg(cfg)
        if p in ['frame_kg', 'battery_kg']:
            test_cfg[p] *= 0.99  # Weight reduction = improvement
        else:
            test_cfg[p] *= 1.01  # Parameter increase = improvement
        sens[p] = evaluate_design(test_cfg) - base_end
    return sens


# =============================================================================
# TEXT REPORTS
# =============================================================================

def print_results(cfg, results):
    SEP  = "=" * 55
    SEP2 = "-" * 55
    w  = results["total_wt"]
    en = results["energy_Wh"]
    pw = results["hover_pwr"]
    ep = results["end_physics"]
    ec = results["end_current"]
    pt = results["payload_table"]

    print("\n" + SEP)
    print("           UAV ENDURANCE RESULTS")
    print(SEP)

    print("\n  WEIGHT BREAKDOWN")
    print("  " + SEP2)
    print("  Frame               : {:.3f} kg".format(cfg["frame_kg"]))
    print("  Battery             : {:.3f} kg".format(cfg["battery_kg"]))
    print("  Payload             : {:.3f} kg".format(cfg["payload_kg"]))
    print("  Electronics         : {:.3f} kg".format(cfg["electronics_kg"]))
    print("  " + SEP2)
    print("  Total Takeoff Weight: {:.3f} kg".format(w))

    print("\n  BATTERY")
    print("  " + SEP2)
    print("  Voltage             : {:.1f} V".format(cfg["voltage_V"]))
    print("  Capacity            : {:.2f} Ah".format(cfg["capacity_Ah"]))
    print("  Total Energy        : {:.2f} Wh".format(en))
    print("  Usable Energy (80%) : {:.2f} Wh".format(en * SAFE_BATTERY_FACTOR))

    print("\n  POWER")
    print("  " + SEP2)
    print("  Hover Power (calc)  : {:.2f} W".format(pw))
    print("  Avg Current (input) : {:.1f} A".format(cfg["avg_current_A"]))

    print("\n  ESTIMATED FLIGHT ENDURANCE")
    print("  " + SEP2)
    print("  Physics-based       : {:.1f} min".format(ep))
    print("  Practical (current) : {:.1f} min".format(ec))
    print("  Recommended range   : {:.1f} - {:.1f} min".format(min(ep, ec), max(ep, ec)))

    print("\n  PAYLOAD SENSITIVITY")
    print("  " + SEP2)
    print("  {:<14}  {:<22}  {}".format("Payload (kg)", "Endurance (min)", "Bar"))
    print("  " + "-" * 50)
    max_end = pt[0][1]
    for p, t in pt:
        bar  = "#" * int((t / max_end) * 25)
        mark = " <-- YOUR PAYLOAD" if abs(p - cfg["payload_kg"]) < 0.01 else ""
        print("  {:<14.1f}  {:<22.1f}  {}{}".format(p, t, bar, mark))

def print_optimization_results(base_end, opt_end, summary):
    SEP  = "=" * 55
    print("\n" + SEP)
    print("          OPTIMIZATION RESULTS")
    print(SEP)
    print(f"\n  Current Endurance: {base_end:.1f} min")
    print("\n  * Payload weight explicitly locked due to mission requirements.")
    
    if not summary:
        print("\n  [!] No further optimization feasible within engineering constraints.")
    else:
        for param, details in summary.items():
            print(f"\n  Change {param}:")
            print(f"  {details['old_val']} \u2192 {details['new_val']}")
            print(f"  Gain: +{details['gain']:.1f} min")

    diff = opt_end - base_end
    pct = (diff / base_end * 100) if base_end > 0 else 0
    print(f"\n  Optimized Endurance:")
    print(f"  {opt_end:.1f} min")
    print(f"  Improvement:")
    print(f"  +{diff:.1f} min (+{pct:.1f}%)\n")


# =============================================================================
# GRAPH THEME HELPER
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


# =============================================================================
# GRAPH GENERATORS
# =============================================================================

def show_graphs(cfg, results):
    """Original dashboard visualization."""
    plt.rcParams.update({
        "figure.facecolor": BG, "font.family": "DejaVu Sans",
        "font.size": 10, "text.color": WHITE,
    })

    fig = plt.figure(figsize=(18, 11), facecolor=BG)
    fig.suptitle("UAV Base Endurance Dashboard", fontsize=18, fontweight="bold", color=WHITE, y=0.98)
    gs = gridspec.GridSpec(2, 3, figure=fig, hspace=0.45, wspace=0.32, left=0.06, right=0.97, top=0.92, bottom=0.07)

    ax1 = fig.add_subplot(gs[0, 0])
    ax2 = fig.add_subplot(gs[0, 1])
    ax3 = fig.add_subplot(gs[0, 2])
    ax4 = fig.add_subplot(gs[1, 0])
    ax5 = fig.add_subplot(gs[1, 1])
    ax6 = fig.add_subplot(gs[1, 2])

    payloads = [row[0] for row in results["payload_table"]]
    durations = [row[1] for row in results["payload_table"]]

    ax1.plot(payloads, durations, marker="o", color=CYAN)
    ax1.set_title("Payload Sensitivity")
    ax1.set_xlabel("Payload (kg)")
    ax1.set_ylabel("Endurance (min)")
    _theme(ax1)

    ax2.bar(["Frame", "Battery", "Payload", "Electronics"],
            [cfg["frame_kg"], cfg["battery_kg"], cfg["payload_kg"], cfg["electronics_kg"]],
            color=[PURPLE, ORANGE, GREEN, CYAN])
    ax2.set_title("Weight Breakdown")
    ax2.set_ylabel("kg")
    _theme(ax2)

    ax3.bar(["Energy", "Usable"],
            [results["energy_Wh"], results["energy_Wh"] * SAFE_BATTERY_FACTOR],
            color=[YELLOW, GREEN])
    ax3.set_title("Battery Energy")
    _theme(ax3)

    ax4.bar(["Hover Power"], [results["hover_pwr"]], color=[ORANGE])
    ax4.set_title("Hover Power")
    _theme(ax4)

    ax5.bar(["Physics", "Current"], [results["end_physics"], results["end_current"]], color=[CYAN, PURPLE])
    ax5.set_title("Endurance Estimates")
    _theme(ax5)

    ax6.text(0.5, 0.5, "UAV Endurance\nEstimator", ha="center", va="center", color=WHITE, fontsize=16)
    ax6.axis("off")

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.show()


def main():
    cfg = collect_inputs()
    warnings = check_feasibility(cfg)
    if warnings:
        print("\n" + "\n".join(["WARNING: " + w for w in warnings]))

    results = calculate(cfg)
    print_results(cfg, results)
    print_engineering_recommendations(cfg, results)

    optimized_cfg, summary, base_end, opt_end = optimize_design(cfg)
    print_optimization_results(base_end, opt_end, summary)

    try:
        show_graphs(cfg, results)
    except Exception:
        pass


if __name__ == "__main__":
    main()
