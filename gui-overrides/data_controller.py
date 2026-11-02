import os
import subprocess
from dataclasses import asdict

import requests
import json
import re
from datetime import datetime, timezone
import docker

import paramiko
from fastapi import APIRouter, Request, Depends
from fastapi.responses import JSONResponse
from fastapi.templating import Jinja2Templates

from src.simulation_objects.simulation import Simulation
from src.simulation_objects.simulation_manager import SimulationManager

influx_data_router = APIRouter()
templates = Jinja2Templates(directory="src/templates")


def get_simulation() -> Simulation:
    return SimulationManager.get_simulation()


@influx_data_router.get("/")
async def root(request: Request, simulation: Simulation = Depends(get_simulation)):
    host_ns3 = os.getenv('NS3_HOST')
    return templates.TemplateResponse(
        "chart.html",
        {
            "request": request,
            "ues": simulation.ues,
            "cells": simulation.cells,
            "sim_id": simulation.sim_id,
            "chart_dimensions": (simulation.max_x, simulation.max_y),
            "host_ns3": host_ns3,
        },
    )

@influx_data_router.get("/scenarios")
async def scenarios(request: Request):
    remote_host = os.getenv("NS3_HOST")

    files = {}

    # Original behavior:
    # obtain the complete ns-3 scenario list from the old
    # host-side GUI trigger server.
    if remote_host:
        try:
            response = requests.get(
                f"http://{remote_host}:38866",
                timeout=3,
            )

            if response.status_code == 200:
                payload = response.json()

                if isinstance(payload, dict):
                    files = dict(payload)

                elif isinstance(payload, list):
                    files = {
                        str(i): value
                        for i, value in enumerate(payload)
                    }

        except Exception as exc:
            print(
                f"[SCENARIOS] Legacy scenario server "
                f"unavailable: {exc}"
            )

    # Preserve the original fallback scenarios.
    if not files:
        files = {
            "0": "scratch/scenario-zero-with_parallel_loging.cc",
            "1": "scratch/scenario-one.cc",
            "2": "scratch/scenario-zero.cc",
            "3": "scratch/scenario-zero-energy.cc",
        }

    # Add slicing without removing or replacing anything.
    slicing = "scratch/scenario-zero-slicing.cc"

    if slicing not in files.values():
        numeric_keys = []

        for key in files:
            try:
                numeric_keys.append(int(key))
            except Exception:
                pass

        next_key = (
            str(max(numeric_keys) + 1)
            if numeric_keys
            else str(len(files))
        )

        files[next_key] = slicing

    return files


@influx_data_router.get("/refresh-data")
async def refresh_data(request: Request, simulation: Simulation = Depends(get_simulation)):
    SimulationManager.refresh_simulation()
    updated_simulation = SimulationManager.get_simulation()
    if (updated_simulation.number_of_ues == 0 or updated_simulation.number_of_cells == 0) and updated_simulation.simulation_status == 'on':
        updated_simulation.set_ue_cell_number()
    es_state = {}
    sinr = {}
    retx = {}
    prb = {}
    for cell in updated_simulation.cells:
        es_state[cell.cell_id] = cell.es_state
        prb[cell.cell_id] = cell.dlPrbUsage_percentage
    for ue in updated_simulation.ues:
        sinr[ue.ue_id] = ue.L3servingSINR_dB
        retx[ue.ue_id] = ue.ErrTotalNbrDl
    print(updated_simulation.ues)
    return {
        "ues": [asdict(ue) for ue in updated_simulation.ues],
        "cells": [asdict(cell) for cell in updated_simulation.cells],
        "max_x_max_y": (updated_simulation.max_x, updated_simulation.max_y),
        "sim_id": updated_simulation.sim_id if updated_simulation.sim_id else 'off',
        "es_state": es_state,
        "sinr": sinr,
        "retx": retx,
        "prb": prb,
        "starting_power": updated_simulation.starting_power,
        "current_power": updated_simulation.current_power,
        "maxec": updated_simulation.maxec,
        "totalcurrec": updated_simulation.totalcurrec,
        "simulation_status": updated_simulation.simulation_status,
    }


@influx_data_router.post("/start_simulation")
async def start_simulation(request: Request):
    form_data = await request.json()
    SimulationManager.reset_simulation()
    remote_host = os.getenv('NS3_HOST')
    if not remote_host:
        print("NS3_HOST environment variable is not set.")
        return
    fields = [
        "e2TermIp",
        "hoSinrDifference",
        "indicationPeriodicity",
        "simTime",
        "KPM_E2functionID",
        "RC_E2functionID",
        "N_MmWaveEnbNodes",
        #"N_LteEnbNodes",
        "N_Ues",
        "CenterFrequency",
        "Bandwidth",
        "N_AntennasMcUe",
        "N_AntennasMmWave",
        "IntersideDistanceUEs",
        "IntersideDistanceCells"
    ]
    scenario = form_data.get('scenario')
    if not scenario:
        return

    flags = False
    if form_data.get('flags') == 'true':
        flags = True

    number_of_ues = int(form_data.get('N_Ues', 5))
    sim_time = float(form_data.get('simTime', 10))

    # =========================================================
    # PORTABLE SLICING LIFECYCLE
    # Only scenario-zero-slicing uses Docker lifecycle here.
    # Existing scenarios continue through the original workflow.
    # =========================================================
    if scenario.endswith('scenario-zero-slicing.cc'):
        try:
            client = docker.from_env(timeout=10)

            # Core services should normally already be running.
            for name in (
                "oran-portable-open5gs",
                "oran-portable-flexric",
                "oran-portable-influxdb",
            ):
                container = client.containers.get(name)
                container.reload()
                if container.status != "running":
                    container.start()

            # Make every GUI Start a fresh slicing run.
            for name in (
                "oran-portable-gui-pusher",
                "oran-portable-gui-bridge",
                "oran-portable-ns3",
            ):
                container = client.containers.get(name)
                container.reload()
                if container.status == "running":
                    container.stop(timeout=3)

            # Start simulator first, then telemetry workers.
            # Pass GUI-selected settings to the portable ns-3 runtime.
            import math
            from pathlib import Path

            if not (1 <= number_of_ues <= 1000):
                raise ValueError("N_Ues must be between 1 and 1000")
            if not (math.isfinite(sim_time) and 0 < sim_time <= 86400):
                raise ValueError("simTime must be between 0 and 86400 seconds")

            config_dir = Path("/runtime-config")
            config_file = config_dir / "simulation.env"
            temp_file = config_dir / "simulation.env.tmp"
            settings = {
                "N_UES": number_of_ues,
                "SIM_TIME": sim_time,
                "N_GNBS": int(form_data.get("N_MmWaveEnbNodes", 3)),
                "BANDWIDTH": float(form_data.get("Bandwidth", 20e6)),
                "FREQUENCY": float(form_data.get("CenterFrequency", 3.5e9)),
                "CELL_DISTANCE": float(form_data.get("IntersideDistanceCells", 1000)),
                "UE_DISTANCE": float(form_data.get("IntersideDistanceUEs", 2)),
                "UE_ANTENNAS": int(form_data.get("N_AntennasMcUe", 1)),
                "GNB_ANTENNAS": int(form_data.get("N_AntennasMmWave", 1)),
                "HO_SINR": float(form_data.get("hoSinrDifference", 3)),
                "E2_PERIOD": float(form_data.get("indicationPeriodicity", 0.1)),
            }
            for key, value in settings.items():
                if not math.isfinite(value) or value <= 0:
                    raise ValueError(f"Invalid {key}: {value}")
            if not 1 <= settings["N_GNBS"] <= 255:
                raise ValueError("Invalid gNB count")
            if not 0.01 <= settings["E2_PERIOD"] <= 2:
                raise ValueError("Invalid E2 periodicity")
            if not 0.1 <= settings["SIM_TIME"] <= 1000:
                raise ValueError("Invalid simulation time")

            temp_file.write_text(
                "".join(f"{key}={value}\n" for key, value in settings.items())
            )
            os.replace(temp_file, config_file)

            # Generate and provision selected UEs inside Open5GS.
            if not 1 <= number_of_ues <= 10:
                raise ValueError("Integrated demo supports 1-10 UEs")
            open5gs_container = client.containers.get(
                "oran-portable-open5gs"
            )
            provision_code = '\nimport csv\nimport subprocess\nimport sys\n\ncount = int(sys.argv[1])\nsource = "/open5gs/subscriber_db.csv"\ntarget = "/tmp/portable-subscribers.csv"\n\nif not 1 <= count <= 10:\n    raise ValueError("Integrated demo supports 1-10 UEs")\n\nwith open(source, newline="") as f:\n    rows = list(csv.reader(f))\n\nheader, existing = rows[0], rows[1:]\nif len(header) != 11 or not existing:\n    raise ValueError("Invalid subscriber CSV")\n\ntemplate = next((r for r in existing if r[0] == "UE1"), existing[0])\nbase_imsi = int(template[1])\n\nby_imsi = {r[1]: r for r in existing}\nselected = []\n\nfor index in range(count):\n    imsi = str(base_imsi + index).zfill(15)\n    if imsi in by_imsi:\n        row = by_imsi[imsi]\n    else:\n        row = template.copy()\n        row[0] = f"UE{index + 1}"\n        row[1] = imsi\n        row[7] = f"10.45.1.{index + 2}"\n        slice_id = index % 3 + 1\n        row[8] = str(slice_id)\n        row[9] = f"{slice_id:06d}"\n        row[10] = ""\n    selected.append(row)\n\nwith open(target, "w", newline="") as f:\n    writer = csv.writer(f)\n    writer.writerow(header)\n    writer.writerows(selected)\n\nsubprocess.run([\n    "python3", "/open5gs/add_users.py",\n    "--mongodb", "127.0.0.1",\n    "--subscriber_data", target,\n], check=True)\n\nprint(f"[PORTABLE] Provisioned {count} UE records", flush=True)\n'
            result = open5gs_container.exec_run(
                ["python3", "-c", provision_code, str(number_of_ues)]
            )
            if result.exit_code != 0:
                raise RuntimeError(
                    "Open5GS provisioning failed: "
                    + result.output.decode(errors="replace")[-2000:]
                )
            print(
                f"[PORTABLE] Auto-provision PASS: {number_of_ues} UEs",
                flush=True,
            )

            ns3_container = client.containers.get("oran-portable-ns3")
            ns3_container.start()

            for name in (
                "oran-portable-gui-bridge",
                "oran-portable-gui-pusher",
            ):
                client.containers.get(name).start()

            SimulationManager._simulation = Simulation(0, 0)
            SimulationManager.start_simulation("scenario-zero-slicing")

            print("[PORTABLE SLICING] ns3 + bridge + pusher started")

            return {
                "status": "started",
                "scenario": "scenario-zero-slicing",
                "runtime": "portable",
            }

        except Exception as e:
            print(f"[PORTABLE SLICING] start failed: {e}")
            return {
                "status": "error",
                "message": str(e),
            }


    # ---------------------------------------------------------
    # Open5GS + Energy Saving integrated experiment
    # ---------------------------------------------------------
    if scenario.endswith('scenario-zero-energy.cc'):
        number_of_cells = 3

        launch_command = (
            f'./run-open5gs-energy-gui.sh '
            f'--ues={number_of_ues} '
            f'--simTime={sim_time}'
        )

        command = (
            f"curl -X POST -d '{launch_command}' "
            f"http://{remote_host}:38866"
        )

        print(
            f'[OPEN5GS GUI] Launching energy experiment: '
            f'{launch_command}'
        )

    # ---------------------------------------------------------
    # Original ns-O-RAN GUI workflow
    # ---------------------------------------------------------
    else:
        if form_data.get('flexric') == 'true':
            arguments = ' '
        else:
            arguments = '--enableE2FileLogging=1 '

        for field in fields:
            value = form_data.get(field)

            if value is not None:
                arguments += f"--{field}={value} "
            elif value is None and field == 'simTime':
                arguments += f"--simTime=100 "

        if flags:
            launch_command = (
                f'./ns3 run --no-build '
                f'"{scenario} {arguments}"'
            )
        else:
            launch_command = (
                f'./ns3 run --no-build '
                f'"{scenario}"'
            )

        command = (
            f"curl -X POST -d '{launch_command}' "
            f"http://{remote_host}:38866"
        )

        number_of_cells = (
            int(form_data.get('N_LteEnbNodes', 1))
            + int(form_data.get('N_MmWaveEnbNodes', 4))
        )

        if not flags:
            number_of_ues = 0
            number_of_cells = 0

    # Measurements will populate the actual UE/cell state.
    SimulationManager._simulation = Simulation(0, 0)

    try:
        print(f'Sending start command: {command}')
        result = subprocess.run(command, shell=True, capture_output=True, text=True)
        print("Response from server:")
        print(result.stdout)
        scenario = os.path.split(scenario)[1].split(".")[0]
        SimulationManager.start_simulation(scenario)
    except Exception as e:
        print(f"An error occurred: {e}")



@influx_data_router.get("/slice-allocation")
async def slice_allocation():
    """
    Return the latest scheduler-side slicing state from ns-3 logs.

    budgetRb is reported only when the scheduler emits SLICE-RRM
    for that slice.  Missing values are returned as null rather
    than guessed.
    """
    container_name = os.getenv(
        "NS3_CONTAINER",
        "oran-portable-ns3",
    )

    try:
        client = docker.from_env(timeout=8)
        container = client.containers.get(container_name)

        if container.status != "running":
            return {
                "status": "Waiting",
                "container": container.status,
                "total_rb": None,
                "slices": {},
            }

        # Read only slicing-relevant lines from the persistent
        # log of the CURRENT run.  runtime-slice.log is cleared
        # whenever ns3-ran starts, so old runs cannot leak in.
        #
        # Filtering before tailing prevents high-volume SLICE-RLC
        # logs from pushing RRM / RC-control events out of view.
        log_result = container.exec_run(
            [
                "sh",
                "-lc",
                (
                    "grep -E "
                    "'\[SLICE-(RRM|ENFORCE)\]|\[RIC-SLICE-CTRL\]' "
                    "/logs/runtime-slice.log 2>/dev/null "
                    "| tail -1000"
                ),
            ]
        )

        if log_result.exit_code == 0:
            logs = log_result.output.decode(
                "utf-8",
                errors="replace",
            )
        else:
            logs = ""

        rrm_pattern = re.compile(
            r"\[SLICE-RRM\].*?"
            r"Slice=(\d+).*?"
            r"active=(\d+).*?"
            r"target=(\d+)%.*?"
            r"budgetRb=(\d+).*?"
            r"rbStart=(\d+).*?"
            r"rbEnd=(\d+).*?"
            r"totalRb=(\d+)"
        )

        enforce_pattern = re.compile(
            r"\[SLICE-ENFORCE\].*?"
            r"Slice=(\d+).*?"
            r"target=(\d+)%.*?"
            r"allocSymbols=(\d+)"
        )

        control_pattern = re.compile(
            r"\[RIC-SLICE-CTRL\].*?"
            r"slice=(\d+).*?"
            r"new=(\d+)"
        )

        slices = {
            "1": {
                "target": None,
                "budget_rb": None,
                "allocated_symbols": None,
                "active": None,
            },
            "2": {
                "target": None,
                "budget_rb": None,
                "allocated_symbols": None,
                "active": None,
            },
            "3": {
                "target": None,
                "budget_rb": None,
                "allocated_symbols": None,
                "active": None,
            },
        }

        total_rb = None

        # Keep one coherent scheduler snapshot.
        # Do not mix Slice 1 from one allocation round with
        # Slice 2/3 from another cell/allocation round.
        latest_rrm_snapshot = None
        current_rrm = {}

        for match in rrm_pattern.finditer(logs):
            slice_id = match.group(1)

            if slice_id not in slices:
                continue

            # Slice 1 starts a new complete allocation round.
            if slice_id == "1":
                current_rrm = {}

            current_rrm[slice_id] = {
                "active": int(match.group(2)),
                "target": int(match.group(3)),
                "budget_rb": int(match.group(4)),
                "rb_start": int(match.group(5)),
                "rb_end": int(match.group(6)),
            }

            if all(x in current_rrm for x in ("1", "2", "3")):
                latest_rrm_snapshot = (
                    {
                        x: dict(current_rrm[x])
                        for x in ("1", "2", "3")
                    },
                    int(match.group(7)),
                )
                current_rrm = {}

        if latest_rrm_snapshot is not None:
            snapshot, total_rb = latest_rrm_snapshot

            for slice_id, values in snapshot.items():
                slices[slice_id].update(values)

        # Same rule for scheduled-symbol values.
        latest_enforce_snapshot = None
        current_enforce = {}

        for match in enforce_pattern.finditer(logs):
            slice_id = match.group(1)

            if slice_id not in slices:
                continue

            if slice_id == "1":
                current_enforce = {}

            current_enforce[slice_id] = {
                "target": int(match.group(2)),
                "allocated_symbols": int(match.group(3)),
            }

            if all(x in current_enforce for x in ("1", "2", "3")):
                latest_enforce_snapshot = {
                    x: dict(current_enforce[x])
                    for x in ("1", "2", "3")
                }
                current_enforce = {}

        if latest_enforce_snapshot is not None:
            for slice_id, values in latest_enforce_snapshot.items():
                slices[slice_id].update(values)


        # -----------------------------------------------------
        # Current policy RB budget
        #
        # This is intentionally kept separate from legacy
        # RRU.PrbUsedDl.  It represents the scheduler policy
        # budget implied by the currently applied slice targets.
        # -----------------------------------------------------

        # Recover total RB even if only a partial SLICE-RRM
        # snapshot was emitted during this run.
        for match in rrm_pattern.finditer(logs):
            total_rb = int(match.group(7))

        # Default policy configured by scenario-zero-slicing.
        policy_targets = {
            "1": 50,
            "2": 30,
            "3": 20,
        }

        # Scheduler/enforcement logs establish the runtime policy.
        for match in enforce_pattern.finditer(logs):
            sid = match.group(1)
            if sid in policy_targets:
                policy_targets[sid] = int(match.group(2))

        # E2SM-RC controls are the newest authoritative policy
        # values after Apply Policy.
        for match in control_pattern.finditer(logs):
            sid = match.group(1)
            if sid in policy_targets:
                policy_targets[sid] = int(match.group(2))

        if total_rb is not None:
            # Largest-remainder allocation preserves total_rb
            # exactly while following the requested target ratio.
            exact = {
                sid: total_rb * ratio / 100.0
                for sid, ratio in policy_targets.items()
            }

            policy_budget = {
                sid: int(value)
                for sid, value in exact.items()
            }

            remaining = total_rb - sum(policy_budget.values())

            order = sorted(
                policy_targets.keys(),
                key=lambda sid: (
                    exact[sid] - policy_budget[sid],
                    -int(sid),
                ),
                reverse=True,
            )

            for sid in order[:remaining]:
                policy_budget[sid] += 1

            for sid in ("1", "2", "3"):
                # Preserve the last observed scheduler value for
                # debugging/research, while exposing the current
                # policy budget to the GUI.
                slices[sid]["observed_budget_rb"] =                     slices[sid].get("budget_rb")

                slices[sid]["target"] = policy_targets[sid]
                slices[sid]["budget_rb"] = policy_budget[sid]

        return {
            "status": "Running",
            "container": "running",
            "total_rb": total_rb,
            "slices": slices,
        }

    except docker.errors.NotFound:
        return {
            "status": "Waiting",
            "message": f"{container_name} not found",
            "total_rb": None,
            "slices": {},
        }

    except Exception as exc:
        return JSONResponse(
            status_code=500,
            content={
                "status": "Failed",
                "message": str(exc),
            },
        )


@influx_data_router.post("/slice-control")
async def slice_control(request: Request):
    """
    Apply one runtime slicing policy through the one-shot FlexRIC xApp.

    Expected JSON:
      {
        "slice1": 40,
        "slice2": 40,
        "slice3": 20
      }
    """
    try:
        payload = await request.json()
    except Exception:
        return JSONResponse(
            status_code=400,
            content={"status": "Failed", "message": "Invalid JSON payload"},
        )

    values = []

    for field in ("slice1", "slice2", "slice3"):
        raw = payload.get(field)

        if isinstance(raw, bool):
            return JSONResponse(
                status_code=400,
                content={
                    "status": "Failed",
                    "message": f"{field} must be an integer from 0 to 100",
                },
            )

        if isinstance(raw, int):
            value = raw
        elif isinstance(raw, str) and raw.strip().isdigit():
            value = int(raw.strip())
        else:
            return JSONResponse(
                status_code=400,
                content={
                    "status": "Failed",
                    "message": f"{field} must be an integer from 0 to 100",
                },
            )

        if value < 0 or value > 100:
            return JSONResponse(
                status_code=400,
                content={
                    "status": "Failed",
                    "message": f"{field} must be between 0 and 100",
                },
            )

        values.append(value)

    slice1, slice2, slice3 = values

    slice_bounds = {
        "slice1": (30, 70),
        "slice2": (20, 50),
        "slice3": (10, 30),
    }

    for name, value in (
        ("slice1", slice1),
        ("slice2", slice2),
        ("slice3", slice3),
    ):
        minimum, maximum = slice_bounds[name]

        if value < minimum or value > maximum:
            return JSONResponse(
                status_code=400,
                content={
                    "status": "Failed",
                    "message": (
                        f"{name} target must be between "
                        f"{minimum} and {maximum}"
                    ),
                },
            )

    if slice1 + slice2 + slice3 != 100:
        return JSONResponse(
            status_code=400,
            content={
                "status": "Failed",
                "message": "Slice target ratios must sum to 100",
            },
        )

    flexric_container = os.getenv(
        "FLEXRIC_CONTAINER",
        "oran-portable-flexric",
    )

    xapp_path = (
        "/opt/flexric/build/examples/xApp/c/ctrl/"
        "xapp_rc_handover_ctrl"
    )

    client = None

    try:
        client = docker.from_env(timeout=35)

        container = client.containers.get(flexric_container)
        container.reload()

        if container.status != "running":
            return JSONResponse(
                status_code=503,
                content={
                    "status": "Failed",
                    "message": (
                        f"FlexRIC container {flexric_container} "
                        f"is not running"
                    ),
                },
            )

        result = container.exec_run(
            cmd=[
                "stdbuf",
                "-o0",
                "-e0",
                xapp_path,
            ],
            environment={
                "SHAK_SLICE1": str(slice1),
                "SHAK_SLICE2": str(slice2),
                "SHAK_SLICE3": str(slice3),
            },
            stdout=True,
            stderr=True,
        )

        output = result.output

        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        else:
            output = str(output)

        node_match = re.search(
            r"Connected E2 nodes\s*=\s*(\d+)",
            output,
        )

        e2_nodes = int(node_match.group(1)) if node_match else 0

        ack_count = output.count(
            "Successfully received CONTROL-ACK"
        )

        expected_acks = e2_nodes * 3

        policy_string = f"{slice1}/{slice2}/{slice3}"

        policy_confirmed = (
            f"requested runtime policy = {policy_string}"
            in output
        )

        applied = (
            result.exit_code == 0
            and e2_nodes > 0
            and ack_count == expected_acks
            and policy_confirmed
        )

        response = {
            "status": "Applied" if applied else "Failed",
            "policy": {
                "slice1": slice1,
                "slice2": slice2,
                "slice3": slice3,
            },
            "e2_nodes": e2_nodes,
            "ack_count": ack_count,
            "expected_acks": expected_acks,
            "xapp_exit_code": result.exit_code,
            "message": (
                f"Policy {policy_string} applied successfully"
                if applied
                else f"Policy {policy_string} was not fully confirmed"
            ),
        }

        if not applied:
            response["xapp_log_tail"] = output.splitlines()[-25:]

        return JSONResponse(
            status_code=200 if applied else 500,
            content=response,
        )

    except docker.errors.NotFound:
        return JSONResponse(
            status_code=503,
            content={
                "status": "Failed",
                "message": (
                    f"FlexRIC container {flexric_container} "
                    f"was not found"
                ),
            },
        )

    except Exception as exc:
        return JSONResponse(
            status_code=500,
            content={
                "status": "Failed",
                "message": f"Slice control error: {exc}",
            },
        )

    finally:
        if client is not None:
            client.close()


@influx_data_router.post("/reset_simulation")
async def reset_simulation():
    SimulationManager.reset_simulation()
    return {"message": "Simulation reset"}


@influx_data_router.post("/stop_simulation")
async def stop_simulation():
    # PORTABLE SLICING STOP
    try:
        client = docker.from_env(timeout=10)
        ns3_container = client.containers.get("oran-portable-ns3")
        ns3_container.reload()

        # Only the slicing scenario uses portable ns3-ran.
        if ns3_container.status == "running":
            for name in (
                "oran-portable-ns3",
                "oran-portable-gui-bridge",
                "oran-portable-gui-pusher",
            ):
                container = client.containers.get(name)
                container.reload()

                if container.status == "running":
                    container.stop(timeout=3)

            SimulationManager.reset_simulation()

            print("[PORTABLE SLICING] stopped")

            return {
                "status": "stopped",
                "scenario": "scenario-zero-slicing",
            }

    except docker.errors.NotFound:
        pass
    except Exception as e:
        print(f"[PORTABLE SLICING] stop check failed: {e}")


    scenario = SimulationManager.get_scenario()
    if not scenario:
        return    
    if not remote_host:
        print("NS3_HOST environment variable is not set.")
        return

    command = f"curl -X POST -d '{scenario}' http://{remote_host}:38867"

    try:
        print(f'Sending stop command: {command}')
        result = subprocess.run(command, shell=True, capture_output=True, text=True)
        print("Response from server:")
        print(result.stdout)
        SimulationManager.stop_simulation()
    except Exception as e:
        print(f"An error occurred: {e}")


