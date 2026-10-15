#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

mkdir -p logs telemetry

echo "Starting O-RAN portable simulator..."
docker compose up -d influxdb gui

echo "Waiting for dashboard..."

for i in $(seq 1 30); do
    if curl -s http://127.0.0.1:8000 >/dev/null 2>&1; then
        echo
        echo "Dashboard ready: http://127.0.0.1:8000"

        if command -v xdg-open >/dev/null 2>&1; then
            xdg-open http://127.0.0.1:8000 >/dev/null 2>&1 &
        fi

        echo
        echo "Select scenario-zero-slicing.cc and click Start new."
        exit 0
    fi

    sleep 1
done

echo "Dashboard did not become ready."
echo "Check: docker logs oran-portable-gui"
exit 1
