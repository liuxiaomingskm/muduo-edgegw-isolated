#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

export MUDUO_USE_POLL=1
export EDGEGW_PINNED=1
export EDGEGW_CONFIG="$SCRIPT_DIR/edge-pin.conf"
export EDGEGW_CHURN=1

exec "$@"
