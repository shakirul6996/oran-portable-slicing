# Portable O-RAN + Open5GS Slicing Demo
A Docker-based portable O-RAN simulation environment integrating ns-3, Open5GS, FlexRIC, InfluxDB, and a web-based dashboard for runtime network slicing experiments.

## Requirements

```text
OS: Ubuntu Linux
Architecture: x86_64
Docker: Docker Engine with Docker Compose v2
Kernel support: SCTP enabled
```

## Quick Start

Clone the repository:
```bash
git clone -b portable-final https://github.com/shakirul6996/oran-portable-slicing.git
cd oran-portable-slicing
```

First-time setup:

```bash
docker compose pull
```

Start the simulator:

```bash
chmod +x run.sh
./run.sh
```

The dashboard opens automatically at:
```bash
http://127.0.0.1:8000
```

<!--
## Quick Start

Clone the repository:

    git clone https://github.com/shakirul6996/oran-portable-slicing.git
    cd oran-portable-slicing

First-time setup:

    ./setup.sh

Start the simulator:

    ./run.sh

The dashboard opens automatically at:

...    http://127.0.0.1:8000
-->
## Components

- ns-3 mmWave/LENA: Radio access network simulation
- Open5GS: 5G Core network with AMF, SMF, and UPF
- FlexRIC Near-RT RIC: RAN control and xApp integration
- E2SM-RC: Runtime slicing policy control
- InfluxDB: Storage of simulation telemetry and KPIs
- Web GUI: Simulation configuration, visualization, and slicing controls


## Note

The Docker images must be accessible from the Docker Hub repository
`shakirulewu/oran-portable`.

If Docker asks for permission to download private images, run:

    docker login

and then run:

    docker compose pull
    
    
 Start the slicing simulation

In the dashboard:

1. Click **Show simulation setup**
2. Select `scratch/scenario-zero-slicing.cc`
3. Click **Start new**
4. Open the **Slicing** tab

Note: N2/N3 connection establishment and simulation startup may take some time. The GUI's initial Waiting state is expected before the simulation starts.

The initial policy is:

```text
Slice 1: 50%
Slice 2: 30%
Slice 3: 20%
```

## 6. Apply a runtime slicing policy

Enter three ratios whose total is `100`.

Example:

```text
Slice 1: 40
Slice 2: 40
Slice 3: 20
```

Click **Apply Policy**.

A successful E2 control should show:

```text
E2 nodes: 4
Control ACKs: 12/12
Status: Applied
```

For `40/40/20` with 28 RB, the policy budget is:

```text
Slice 1: 11 RB
Slice 2: 11 RB
Slice 3: 6 RB
```

Control path:

```text
GUI -> FlexRIC xApp -> E2SM-RC -> ns-3 gNB
```

## 7. Stop the simulation

Click **Stop** in the dashboard.

Verify that the runtime workers stopped:

```bash
docker ps --format '{{.Names}}' | \
grep -E 'oran-portable-(ns3|gui-bridge|gui-pusher)' \
|| echo "[PASS] simulation stopped"
```

To stop the remaining dashboard/core containers:

```bash
cd portable
docker compose down
```

## Troubleshooting

Check container state:

```bash
cd portable
docker compose ps -a
```

Check GUI logs:

```bash
docker logs --tail 100 oran-portable-gui
```

Check ns-3 logs:

```bash
docker logs --tail 100 oran-portable-ns3
```

## Notes

- The Slicing tab shows the current **slice policy RB budget**.
- This does not replace the legacy `RRU.PrbUsedDl` metric.
- `portable/logs/` and `portable/telemetry/` are runtime directories and should not be committed.   
    
