#!/usr/bin/env bash
set -e

echo "[1/4] Checking Docker..."

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker not found. Installing Docker..."
    sudo apt update
    sudo apt install -y curl ca-certificates
    curl -fsSL https://get.docker.com -o /tmp/get-docker.sh
    sudo sh /tmp/get-docker.sh
    sudo usermod -aG docker "$USER"
    echo "Docker installed. Please log out/in once, then run ./setup.sh again."
    exit 0
fi

docker compose version >/dev/null

echo "[2/4] Preparing directories..."
mkdir -p logs telemetry

echo "[3/4] Downloading Docker images..."

pull_image () {
    docker pull "$1" || {
        echo
        echo "Docker image pull failed."
        echo "If the repository is private, run: docker login"
        echo "Then run ./setup.sh again."
        exit 1
    }
}

pull_image shakirulewu/oran-portable:open5gs-v1
pull_image shakirulewu/oran-portable:flexric-v1
pull_image shakirulewu/oran-portable:ns3-v1
pull_image shakirulewu/oran-portable:gui-v1
pull_image influxdb:1.8-alpine

echo "[4/4] Preparing image names..."

docker tag shakirulewu/oran-portable:open5gs-v1 oran-portable-open5gs:v1
docker tag shakirulewu/oran-portable:flexric-v1 oran-portable-flexric:v1
docker tag shakirulewu/oran-portable:ns3-v1 oran-portable-ns3:v1
docker tag shakirulewu/oran-portable:gui-v1 oran-portable-gui:v1

echo
echo "======================================"
echo " Setup complete"
echo " Run the simulator with: ./run.sh"
echo "======================================"
