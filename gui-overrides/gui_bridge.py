#!/usr/bin/env python3

import csv
import glob
import math
import os
import re
import time
from pathlib import Path

# ------------------------------------------------------------
# GUI bridge for scenario-zero-energy + Open5GS experiment
# ------------------------------------------------------------

CELLS = {
    2: (2000.0, 2000.0),
    3: (3000.0, 2000.0),
    4: (1000.0, 2000.0),
}

# Official fresh 10 s baseline:
# Cell2 9968.973736
# Cell3 6657.183736
# Cell4 15829.533736
BASELINE_TOTAL_J = 32455.691208

SIM_ID = os.getenv("GUI_SIM_ID", "open5gs-es-demo")

try:
    UE_COUNT = int(os.getenv("GUI_UE_COUNT", "5"))
except ValueError:
    UE_COUNT = 5

# Keep the last serving-cell information even after pusher clears KPI files.
serving_cells = {}

# Schematic offsets only for GUI topology rendering.
# Serving-cell association itself comes from CU-CP data.
UE_OFFSETS = {
    1: (-150, -130),
    2: (120, -120),
    3: (-130, 130),
    4: (140, 110),
    5: (0, 180),
}


def atomic_write(filename, content):
    tmp = filename + ".bridge.tmp"
    with open(tmp, "w") as f:
        f.write(content)
    os.replace(tmp, filename)


def safe_float(v):
    try:
        return float(v)
    except Exception:
        return None


def scan_cucp_serving_cells():
    """
    Read current CU-CP files and remember the most recent cell seen for each UE.

    Current experiment files use UE identity in column 2.
    """
    global serving_cells

    latest = {}

    for path in glob.glob("cu-cp-cell-*.txt"):
        m = re.search(r"cell-(\d+)", path)
        if not m:
            continue

        cell_id = int(m.group(1))

        try:
            with open(path, newline="") as f:
                reader = csv.reader(f)
                rows = list(reader)
        except Exception:
            continue

        if len(rows) <= 1:
            continue

        seq = 0

        for row in rows[1:]:
            if len(row) < 2:
                continue

            ue_raw = row[1].strip()

            try:
                ue_id = int(float(ue_raw))
            except Exception:
                continue

            if ue_id <= 0:
                continue

            seq += 1

            # Prefer timestamp if numeric; otherwise preserve row order.
            timestamp = safe_float(row[0].strip())
            ordering = timestamp if timestamp is not None else seq

            old = latest.get(ue_id)

            if old is None or ordering >= old[0]:
                latest[ue_id] = (ordering, cell_id)

    for ue_id, (_, cell_id) in latest.items():
        serving_cells[ue_id] = cell_id


def read_energy_file(cell_id):
    """
    Returns:
        (last_sim_time, cumulative_energy_j)
    """
    path = Path(f"energyfilecell{cell_id}.csv")

    if not path.exists():
        return None, None

    last_time = None
    last_energy = None

    try:
        with path.open(newline="") as f:
            reader = csv.reader(f)
            next(reader, None)

            for row in reader:
                if len(row) < 2:
                    continue

                t = safe_float(row[0].strip())
                e = safe_float(row[1].strip())

                if t is not None and e is not None:
                    last_time = t
                    last_energy = e

    except Exception:
        return None, None

    return last_time, last_energy


def get_energy_state():
    energy = {}
    trace_times = {}

    for cell_id in get_topology(get_runtime_settings()):
        t, e = read_energy_file(cell_id)

        if t is not None:
            trace_times[cell_id] = t

        if e is not None:
            energy[cell_id] = e

    max_trace_time = max(trace_times.values()) if trace_times else 0.0

    es_state = {}

    for cell_id in get_topology(get_runtime_settings()):
        t = trace_times.get(cell_id)

        # In our energy model an OFF cell stops accumulating energy.
        # Use a generous 1 s gap to avoid transient false OFF states.
        is_off = (
            t is not None
            and max_trace_time >= 2.0
            and (max_trace_time - t) > 1.0
        )

        es_state[cell_id] = 1 if is_off else 0

    current_total = sum(energy.values())

    return energy, es_state, current_total


def write_gnbs():
    energy, es_state, current_total = get_energy_state()

    timestamp = int(time.time() * 1000)

    lines = [
        "timestamp,id,x,y,simid,ESstate,ESpower,maxEC,totalcurrEC"
    ]

    for cell_id, (x, y) in get_topology(get_runtime_settings()).items():
        cell_energy = energy.get(cell_id, 0.0)

        lines.append(
            f"{timestamp},"
            f"{cell_id},"
            f"{x},"
            f"{y},"
            f"{SIM_ID},"
            f"{es_state.get(cell_id, 0)},"
            f"{cell_energy:.6f},"
            f"{0.0:.6f},"  # No matched baseline available
            f"{current_total:.6f}"
        )

    atomic_write("gnbs.txt", "\n".join(lines) + "\n")


def get_runtime_ue_count():
    try:
        config = Path("/runtime-config/simulation.env")
        for line in config.read_text().splitlines():
            if line.startswith("N_UES="):
                return max(1, min(1000, int(line.split("=", 1)[1])))
    except (OSError, ValueError):
        pass
    return UE_COUNT


def get_runtime_settings():
    settings = {}
    try:
        for line in Path("/runtime-config/simulation.env").read_text().splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                settings[key] = value.strip()
    except OSError:
        pass
    return settings


def get_radio_cells_from_log():
    """Read observed mmWave associations from current ns-3 run."""
    result = {}
    path = Path("/runtime-logs/runtime-slice.log")
    try:
        with path.open("r", errors="replace") as f:
            # The current run logs registration and attachment near startup.
            # Bound the scan to avoid rereading large RLC traces.
            for index, line in enumerate(f):
                if index >= 100000:
                    break
                match = re.search(r"\[SLICE-STATE\] IMSI=(\d+) mmCell=(\d+)", line)
                if match:
                    result[int(match.group(1))] = int(match.group(2))
    except OSError:
        pass
    return result


def get_topology(settings):
    n_gnbs = max(1, int(settings.get("N_GNBS", 3)))
    distance = float(settings.get("CELL_DISTANCE", 1000))

    topology = {2: (2000.0, 2000.0)}
    for index in range(1, n_gnbs):
        angle = 2 * math.pi * (index - 1) / max(1, n_gnbs - 1)
        topology[index + 2] = (
            2000 + distance * math.cos(angle),
            2000 + distance * math.sin(angle)
        )
    return topology


def write_ue_positions():
    settings = get_runtime_settings()
    spacing = float(settings.get("UE_DISTANCE", 2))
    count = get_runtime_ue_count()
    observed_cells = get_radio_cells_from_log()

    timestamp = int(time.time() * 1000)
    lines = ["timestamp,id,x,y,type,cell,simid"]

    for ue_id in range(1, count + 1):
        # Exact deterministic placement used in scenario-zero-slicing.cc
        x = 2005.0 + spacing * (ue_id - 1)
        y = 2005.0

        # Cell comes from observed radio state, never an invented association.
        cell_id = observed_cells.get(ue_id, 0)

        lines.append(
            f"{timestamp},{ue_id},{x},{y},mmwave,{cell_id},{SIM_ID}"
        )

    atomic_write("ue_position.txt", "\n".join(lines) + "\n")


def print_status():
    energy, es_state, current_total = get_energy_state()

    print(
        "[GUI BRIDGE] "
        f"UE cells={serving_cells} | "
        f"ES={es_state} | "
        f"energy={current_total:.3f} J | "
        f"saving=N/A (matched baseline unavailable)",
        flush=True,
    )


def main():
    print("[GUI BRIDGE] Starting Open5GS / Energy GUI bridge", flush=True)

    # Current project has no LTE eNB telemetry.
    # Avoid feeding the old gnuplot-formatted enbs.txt into the CSV pusher.
    if os.path.exists("enbs.txt"):
        backup = "enbs.txt.before_gui_bridge"

        if not os.path.exists(backup):
            try:
                os.rename("enbs.txt", backup)
                print(
                    f"[GUI BRIDGE] Saved old enbs.txt -> {backup}",
                    flush=True,
                )
            except Exception:
                pass

    last_print = 0

    while True:
        scan_cucp_serving_cells()

        write_gnbs()
        write_ue_positions()

        now = time.time()

        if now - last_print >= 5:
            print_status()
            last_print = now

        time.sleep(1)


if __name__ == "__main__":
    main()
