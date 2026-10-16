# Portable O-RAN + Open5GS Slicing 

## Requirements

```text
OS: Ubuntu Linux
Architecture: x86_64
Docker: Docker Engine with Docker Compose v2
Kernel support: SCTP enabled

**## Quick Start**

Clone the repository:

    git clone https://github.com/shakirul6996/oran-portable-slicing.git
    cd oran-portable-slicing

First-time setup:

    ./setup.sh

Start the simulator:

    ./run.sh

The dashboard opens automatically at:

    http://127.0.0.1:8000

## Components

- ns-3 mmWave/LENA
- Open5GS
- FlexRIC Near-RT RIC
- E2SM-RC runtime slicing
- InfluxDB
- Web GUI

## Note

The Docker images must be accessible from the Docker Hub repository
`shakirulewu/oran-portable`.

If Docker asks for permission to download private images, run:

    docker login

and then run:

    ./setup.sh
    
    
 Start the slicing simulation

In the dashboard:

1. Click **Show simulation setup**
2. Select `scratch/scenario-zero-slicing.cc`
3. Click **Start new**
4. Open the **Slicing** tab

Wait until Open5GS, N2/NGAP and N3/GTP-U become active.

The initial policy is:

```text
Slice 1: 50%
Slice 2: 30%
Slice 3: 20%
```

## Apply a runtime slicing policy

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

##  Stop the simulation

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
    
