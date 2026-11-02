#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

mkdir -p logs telemetry

echo "Starting Open5GS, FlexRIC and InfluxDB..."
docker compose up -d open5gs flexric influxdb

echo "Preparing simulator and telemetry containers..."
docker compose create ns3-ran gui-bridge gui-pusher

echo "Starting dashboard..."
docker compose up -d --no-deps gui

for i in $(seq 1 30); do
    if curl -fsS http://127.0.0.1:8000/ >/dev/null 2>&1; then
        echo "Dashboard ready: http://127.0.0.1:8000"
        echo "Select scenario-zero-slicing.cc and click Start new."

        if command -v xdg-open >/dev/null 2>&1; then
            xdg-open http://127.0.0.1:8000/ >/dev/null 2>&1 &
        fi
        exit 0
    fi
    sleep 1
done

echo "Dashboard did not become ready."
echo "Check: docker compose logs --tail=80 gui"
exit 1
