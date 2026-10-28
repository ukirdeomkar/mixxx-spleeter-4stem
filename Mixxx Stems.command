#!/bin/bash
# Launch the Spleeter 4-stem build of Mixxx.
cd "$(dirname "$0")"
exec ./build/mixxx "$@"
