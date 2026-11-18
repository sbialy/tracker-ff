# Sourced by the sims that run the BRIDGE=1 ui_shot (emu parity plan T10, ref/emu-parity-plan.md). Host only, NOT firmware.
# A synth .plg, a master insert .plg and a Sample Editor .ptfx are Thumb code: only the BRIDGE=1 build runs them (their real card bytes under
# libunicorn, host_ucbridge.cpp). This file finds what that build needs and sets, for the caller:
#   UNICORN_DIR   include/unicorn/unicorn.h and lib/libunicorn.a of the pinned wheel unicorn-2.1.4-cp37-abi3-macosx_11_0_arm64.whl (sha256 below).
#                 Taken from the environment, else ROOT/build/unicorn, else unpacked there from the wheel ($UNICORN_WHEEL, the wheel in ROOT, in
#                 the main checkout ~/prj/poly or in the pinned emu's cache ~/prj/poly/build/emu/cache), the sha256 is checked first
#   BRIDGE_AXF    the release axf the bridge maps (its loader, its libm, the device addresses of the windows). From the environment, else this
#                 tree's make original-release output platforms/mk66/build-trim/PolyendTracker.axf, built here when it is missing
#                 (TOOLCHAIN, default agt/12.3.rel1 of this tree or of ~/prj/poly)
#   BRIDGE_ARGS   the make arguments of the build: BRIDGE=1 UNICORN_DIR=... BRIDGE_AXF=...
# bridge_env returns non zero (and says why on stderr) when one of them cannot be had. Unicorn is GPL-2.0: internal tool only.
UNICORN_WHEEL_NAME=unicorn-2.1.4-cp37-abi3-macosx_11_0_arm64.whl
UNICORN_WHEEL_SHA256=d6c93e0f60328d8f4a1792af3f834137a28050fcc2305f2ec01efe8558a9844e
bridge_env() {   # bridge_env ROOT
    local root="$1" whl="" w tmp tc
    if [ -z "${UNICORN_DIR:-}" ]; then
        UNICORN_DIR="$root/build/unicorn"
        if [ ! -f "$UNICORN_DIR/lib/libunicorn.a" ] || [ ! -f "$UNICORN_DIR/include/unicorn/unicorn.h" ]; then
            for w in "${UNICORN_WHEEL:-}" "$root/$UNICORN_WHEEL_NAME" "$HOME/prj/poly/$UNICORN_WHEEL_NAME" "$HOME/prj/poly/build/emu/cache/$UNICORN_WHEEL_NAME"; do
                [ -n "$w" ] && [ -f "$w" ] && { whl="$w"; break; }
            done
            [ -n "$whl" ] || { echo "bridge_env: no $UNICORN_WHEEL_NAME (set UNICORN_WHEEL or UNICORN_DIR)" >&2; return 1; }
            [ "$(shasum -a 256 "$whl" | cut -d' ' -f1)" = "$UNICORN_WHEEL_SHA256" ] || { echo "bridge_env: $whl is not the pinned wheel (sha256)" >&2; return 1; }
            tmp="$(mktemp -d "${TMPDIR:-/tmp}/bridge_env.XXXXXX")"   # the wheel is data: unpack into an empty folder, take the two trees only
            unzip -q "$whl" 'unicorn/include/*' 'unicorn/lib/libunicorn.a' -d "$tmp" || { rm -rf "$tmp"; echo "bridge_env: cannot unpack $whl" >&2; return 1; }
            rm -rf "$UNICORN_DIR"; mkdir -p "$UNICORN_DIR/lib"
            mv "$tmp/unicorn/include" "$UNICORN_DIR/include"; mv "$tmp/unicorn/lib/libunicorn.a" "$UNICORN_DIR/lib/libunicorn.a"; rm -rf "$tmp"
        fi
    fi
    [ -f "$UNICORN_DIR/lib/libunicorn.a" ] || { echo "bridge_env: no $UNICORN_DIR/lib/libunicorn.a" >&2; return 1; }
    if [ -z "${BRIDGE_AXF:-}" ]; then
        BRIDGE_AXF="$root/platforms/mk66/build-trim/PolyendTracker.axf"
        if [ ! -f "$BRIDGE_AXF" ]; then
            tc="${TOOLCHAIN:-$root/agt/12.3.rel1/bin/arm-none-eabi-}"
            [ -x "${tc}gcc" ] || tc="$HOME/prj/poly/agt/12.3.rel1/bin/arm-none-eabi-"
            echo "bridge_env: no release axf yet, make original-release ($tc)" >&2
            make -s -C "$root" original-release TOOLCHAIN="$tc" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >"$root/build/bridge_env_original.log" 2>&1 \
                || { tail -20 "$root/build/bridge_env_original.log" >&2; echo "bridge_env: make original-release failed (set BRIDGE_AXF)" >&2; return 1; }
        fi
    fi
    [ -f "$BRIDGE_AXF" ] || { echo "bridge_env: no release axf $BRIDGE_AXF" >&2; return 1; }
    export UNICORN_DIR BRIDGE_AXF
    BRIDGE_ARGS=(BRIDGE=1 UNICORN_DIR="$UNICORN_DIR" BRIDGE_AXF="$BRIDGE_AXF")
}
