#!/bin/bash
# Builds the CloudpilotEmu web app with a writable PXA real-time clock and
# serves it on http://127.0.0.1:18765/ - the test emulator for DateFix.
#
# Requires: git, emsdk 6.0.0 (~/tools/emsdk, as in CloudpilotEmu's CI),
# node + yarn, python3. Usage: tools/emulator/build.sh <work directory>
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${1:?usage: build.sh <work directory>}"
EMSDK_DIR="${EMSDK_DIR:-$HOME/tools/emsdk}"

mkdir -p "$WORK"
cd "$WORK"
[ -d cloudpilot ] || git clone https://github.com/cloudpilot-emu/cloudpilot-emu.git cloudpilot
cd cloudpilot
git checkout -q "$(cat "$HERE/CLOUDPILOT_COMMIT")"
git apply --check "$HERE/cloudpilot-writable-rtc.patch" 2>/dev/null && git apply "$HERE/cloudpilot-writable-rtc.patch"

# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh" >/dev/null
make -C src -j8 emscripten
(cd web && yarn install --frozen-lockfile && yarn build)

echo "serving on http://127.0.0.1:18765/"
exec python3 "$HERE/serve.py" "$WORK/cloudpilot/web/build-pwa"
