"""
UAV Endurance Estimator — with Graphs
======================================
Requires ONLY matplotlib (no numpy needed).

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
# NUMPY-FREE HELPERS  (replaces np.linspace / np.array)
# =============================================================================

def linspace(start, stop, n):
    """Pure-Python replacement for numpy.linspace."""
    if n <= 1:
        return [start]
    step = (stop - start) / (n - 1)
    return [start + i * step for i in range(n)]


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
    battery_kg     = get_float("  Battery weight        (kg) : ", allow_zero=False)
    payload_kg     = get_float("  Payload weight        (kg) : ", allow_zero=True)
    electronics_kg = get_float("  Electronics weight    (kg) : ", allow_zero=True)

    print("\n  STEP 2: BATTERY PARAMETERS")
    print("  " + SEP2)
    print("  Common voltages: 3S=11.1V  |  4S=14.8V  |  6S=22.2V")
    voltage_V     = get_float("  Battery voltage       (V)  : ", allow_zero=False)
    capacity_Ah   = get_float("  Battery capacity      (Ah) : ", allow_zero=False)
    print("  Tip: enter measured average current during hover")
    avg_current_A = get_float("  Average current draw  (A)  : ", allow_zero=False)

    print("\n  STEP 3: ROTOR AND MOTOR PARAMETERS")
    print("  " + SEP2)
    num_rotors = get_int("  Number of rotors           : ", min_val=1)
    print("  Tip: 10-inch=0.254m  |  12-inch=0.305m  |  15-inch=0.381m")
    rotor_diameter_m = get_float("  Rotor diameter        (m)  : ", allow_zero=False)
    print("  Typical range: 0.65 to 0.85  (motor + ESC combined)")
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
# TEXT RESULTS REPORT
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

    print("\n  DESIGN TIPS")
    print("  " + SEP2)
    if ep < 10:
        print("  [!] Very low endurance. Use higher-capacity battery or reduce weight.")
    elif ep < 20:
        print("  [~] Moderate endurance. Larger props or lighter frame would help.")
    else:
        print("  [OK] Good endurance. Fine-tune efficiency and prop size for more gains.")
    print("\n" + SEP + "\n")


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
# GRAPH GENERATOR
# =============================================================================

def show_graphs(cfg, results):
    plt.rcParams.update({
        "figure.facecolor": BG,
        "font.family"     : "DejaVu Sans",
        "font.size"       : 10,
        "text.color"      : WHITE,
    })

    fig = plt.figure(figsize=(18, 11), facecolor=BG)
    fig.suptitle("UAV Endurance Dashboard", fontsize=18,
                 fontweight="bold", color=WHITE, y=0.98)

    gs = gridspec.GridSpec(2, 3, figure=fig,
                           hspace=0.45, wspace=0.32,
                           left=0.06, right=0.97,
                           top=0.92, bottom=0.07)

    ax1 = fig.add_subplot(gs[0, 0])
    ax2 = fig.add_subplot(gs[0, 1])
    ax3 = fig.add_subplot(gs[0, 2])
    ax4 = fig.add_subplot(gs[1, 0])
    ax5 = fig.add_subplot(gs[1, 1])
    ax6 = fig.add_subplot(gs[1, 2])

    base_w  = cfg["frame_kg"] + cfg["battery_kg"] + cfg["electronics_kg"]
    total_w = results["total_wt"]
    pwr_tot = results["hover_pwr"]
    ep      = results["end_physics"]
    N       = 200   # number of points per curve

    # ── GRAPH 1: Payload vs Endurance ─────────────────────────────────────────
    pls  = linspace(0, 5, N)
    ends = [endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"],
                hover_power_physics(base_w + p, cfg["num_rotors"],
                                    cfg["rotor_diameter_m"], cfg["motor_efficiency"]))
            for p in pls]
    ax1.plot(pls, ends, color=CYAN, lw=2.2, zorder=3)
    ax1.fill_between(pls, ends, alpha=0.13, color=CYAN)
    ax1.axvline(cfg["payload_kg"], color=YELLOW, lw=1.6, ls="--", label="Your payload")
    ax1.axhline(ep,                color=GREEN,  lw=1.4, ls=":",  label="Your endurance")
    ax1.scatter([cfg["payload_kg"]], [ep], color=YELLOW, s=70, zorder=6)
    ax1.annotate("{:.1f} min".format(ep),
                 xy=(cfg["payload_kg"], ep), xytext=(8, 6),
                 textcoords="offset points", color=YELLOW, fontsize=8.5)
    ax1.set_xlabel("Payload (kg)")
    ax1.set_ylabel("Endurance (min)")
    ax1.set_title("Payload vs Endurance")
    ax1.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax1)

    # ── GRAPH 2: Battery Voltage vs Endurance ─────────────────────────────────
    volts  = linspace(7.4, 29.6, N)
    v_ends = [endurance_energy_method(v, cfg["capacity_Ah"], pwr_tot) for v in volts]
    ax2.plot(volts, v_ends, color=ORANGE, lw=2.2, zorder=3)
    ax2.fill_between(volts, v_ends, alpha=0.13, color=ORANGE)
    for vv, lbl in [(11.1, "3S"), (14.8, "4S"), (22.2, "6S"), (25.9, "7S")]:
        ax2.axvline(vv, color=BORDER, lw=1, ls=":")
        ax2.text(vv + 0.3, min(v_ends) + 0.5, lbl, color=DIM, fontsize=8)
    ax2.axvline(cfg["voltage_V"], color=YELLOW, lw=1.6, ls="--", label="Your voltage")
    ax2.scatter([cfg["voltage_V"]], [ep], color=YELLOW, s=70, zorder=6)
    ax2.set_xlabel("Battery Voltage (V)")
    ax2.set_ylabel("Endurance (min)")
    ax2.set_title("Battery Voltage vs Endurance")
    ax2.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax2)

    # ── GRAPH 3: Battery Capacity vs Endurance ────────────────────────────────
    caps   = linspace(1, 25, N)
    c_ends = [endurance_energy_method(cfg["voltage_V"], c, pwr_tot) for c in caps]
    ax3.plot(caps, c_ends, color=PURPLE, lw=2.2, zorder=3)
    ax3.fill_between(caps, c_ends, alpha=0.13, color=PURPLE)
    ax3.axvline(cfg["capacity_Ah"], color=YELLOW, lw=1.6, ls="--", label="Your capacity")
    ax3.scatter([cfg["capacity_Ah"]], [ep], color=YELLOW, s=70, zorder=6)
    ax3.annotate("{:.1f} min".format(ep),
                 xy=(cfg["capacity_Ah"], ep), xytext=(8, -14),
                 textcoords="offset points", color=YELLOW, fontsize=8.5)
    ax3.set_xlabel("Battery Capacity (Ah)")
    ax3.set_ylabel("Endurance (min)")
    ax3.set_title("Battery Capacity vs Endurance")
    ax3.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax3)

    # ── GRAPH 4: Weight Breakdown Donut ───────────────────────────────────────
    raw_sizes  = [cfg["frame_kg"], cfg["battery_kg"],
                  cfg["payload_kg"], cfg["electronics_kg"]]
    raw_labels = [
        "Frame\n{:.2f} kg".format(cfg["frame_kg"]),
        "Battery\n{:.2f} kg".format(cfg["battery_kg"]),
        "Payload\n{:.2f} kg".format(cfg["payload_kg"]),
        "Electronics\n{:.2f} kg".format(cfg["electronics_kg"]),
    ]
    raw_colors = [CYAN, GREEN, ORANGE, PURPLE]
    # drop zero slices to avoid matplotlib warning
    filtered = [(s, l, c) for s, l, c in
                zip(raw_sizes, raw_labels, raw_colors) if s > 0]
    nz_sizes  = [x[0] for x in filtered]
    nz_labels = [x[1] for x in filtered]
    nz_colors = [x[2] for x in filtered]

    wedges, texts, autotexts = ax4.pie(
        nz_sizes, labels=nz_labels, colors=nz_colors,
        explode=[0.03] * len(nz_sizes),
        autopct="%1.1f%%", startangle=110,
        wedgeprops=dict(width=0.52, edgecolor=BG, linewidth=2),
        textprops=dict(color=WHITE, fontsize=8.5),
    )
    for at in autotexts:
        at.set_color(BG)
        at.set_fontweight("bold")
        at.set_fontsize(8)
    ax4.set_facecolor(PANEL)
    ax4.set_title("Weight Breakdown  ({:.2f} kg total)".format(total_w))
    ax4.title.set_color(WHITE)

    # ── GRAPH 5: Rotor Diameter vs Endurance ──────────────────────────────────
    dias   = linspace(0.10, 0.60, N)
    d_ends = [endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"],
                  hover_power_physics(total_w, cfg["num_rotors"],
                                      dia, cfg["motor_efficiency"]))
              for dia in dias]
    dias_cm = [d * 100 for d in dias]
    ax5.plot(dias_cm, d_ends, color=PINK, lw=2.2, zorder=3)
    ax5.fill_between(dias_cm, d_ends, alpha=0.13, color=PINK)
    ax5.axvline(cfg["rotor_diameter_m"] * 100, color=YELLOW,
                lw=1.6, ls="--", label="Your rotor")
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

    # ── GRAPH 6: Motor Efficiency vs Endurance ────────────────────────────────
    etas   = linspace(0.30, 0.98, N)
    e_ends = [endurance_energy_method(cfg["voltage_V"], cfg["capacity_Ah"],
                  hover_power_physics(total_w, cfg["num_rotors"],
                                      cfg["rotor_diameter_m"], eta))
              for eta in etas]
    etas_pct = [e * 100 for e in etas]
    ax6.plot(etas_pct, e_ends, color=GREEN, lw=2.2, zorder=3)
    ax6.fill_between(etas_pct, e_ends, alpha=0.13, color=GREEN)
    ax6.axvline(cfg["motor_efficiency"] * 100, color=YELLOW,
                lw=1.6, ls="--", label="Your efficiency")
    ax6.scatter([cfg["motor_efficiency"] * 100], [ep], color=YELLOW, s=70, zorder=6)
    ax6.annotate("{:.1f} min".format(ep),
                 xy=(cfg["motor_efficiency"] * 100, ep), xytext=(6, 6),
                 textcoords="offset points", color=YELLOW, fontsize=8.5)
    ax6.set_xlabel("Motor Efficiency (%)")
    ax6.set_ylabel("Endurance (min)")
    ax6.set_title("Motor Efficiency vs Endurance")
    ax6.legend(fontsize=8, facecolor=PANEL, edgecolor=BORDER, labelcolor=WHITE)
    _theme(ax6)

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.show()


# =============================================================================
# MAIN
# =============================================================================

def main():
    try:
        while True:
            cfg     = collect_inputs()
            results = calculate(cfg)
            print_results(cfg, results)
            print("  Opening graphs window... (close it to continue)\n")
            show_graphs(cfg, results)

            while True:
                again = input("  Run another calculation? (y/n) : ").strip().lower()
                if again == "y":
                    break
                elif again == "n":
                    print("\n  Goodbye!\n")
                    sys.exit(0)
                else:
                    print("  [!] Please enter y or n.")

    except KeyboardInterrupt:
        print("\n\n  [Interrupted] Goodbye!\n")
        sys.exit(0)
    except ZeroDivisionError:
        print("\n  [ERROR] Division by zero. Check power/current values.")
        sys.exit(1)
    except Exception as e:
        print("\n  [ERROR] {}".format(e))
        sys.exit(1)


if __name__ == "__main__":
    main()