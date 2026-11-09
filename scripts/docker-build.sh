#!/usr/bin/env bash
# Build the arm64 Linux image and run clean `make original` builds inside it.
#
# usage: scripts/docker-build.sh [bind|copy] [runs]
#   bind  repo bind-mounted at /work (build output lands in the host tree)
#   copy  repo copied into the container filesystem first (no mount I/O cost, host tree untouched)
#   runs  number of clean builds, default 1 (prints wall time per run and the -j used)
#
# env: IMAGE (default tracker-ff-build-arm64), DOCKER (default docker)
set -euo pipefail

mode="${1:-bind}"
runs="${2:-1}"
case "$mode" in bind|copy) ;; *) echo "mode must be bind or copy" >&2; exit 2 ;; esac

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${IMAGE:-tracker-ff-build-arm64}"
docker="${DOCKER:-docker}"

"$docker" build --platform linux/arm64 -t "$image" -f "$repo/docker/Dockerfile" "$repo/docker"

# Runs inside the container (single-quoted on purpose). .git must be present: BUILD_TAG comes from it.
inner='
set -u
if [ "$MODE" = copy ]; then
  mkdir -p /build-copy
  tar -C /work --exclude=./agt --exclude=./build --exclude=./core/build-\* --exclude=./platforms/mk66/build -cf - . | tar -C /build-copy -xf -
  cd /build-copy
fi
echo "mode=$MODE dir=$(pwd) nproc=$(nproc) -> make uses -j$(nproc)"
for i in $(seq 1 "$RUNS"); do
  make clean >/dev/null 2>&1
  s=$(date +%s.%N)
  make original TOOLCHAIN="$TOOLCHAIN" >"/tmp/build-$i.log" 2>&1
  rc=$?
  e=$(date +%s.%N)
  awk -v s="$s" -v e="$e" -v i="$i" -v rc="$rc" "BEGIN{printf \"run %s: %.2fs rc=%s\n\", i, e-s, rc}"
  [ "$rc" -eq 0 ] || { tail -30 "/tmp/build-$i.log"; exit "$rc"; }
done
sha256sum build/PolyendTracker_CE_1.0.0.ptf
'

exec "$docker" run --rm --platform linux/arm64 \
  -v "$repo:/work" -e MODE="$mode" -e RUNS="$runs" \
  "$image" bash -c "$inner"
