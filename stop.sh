#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

docker compose down

echo "Portable O-RAN simulator stopped."
