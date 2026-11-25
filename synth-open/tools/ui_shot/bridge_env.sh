# Sourced by the sims that run the BRIDGE=1 ui_shot (emu parity plan T10, ref/emu-parity-plan.md). Host only, NOT firmware.
# A synth .plg, a master insert .plg and a Sample Editor .ptfx are Thumb code: only the BRIDGE=1 build runs them (their real card bytes under
# libunicorn, host_ucbridge.cpp). This file finds what that build needs and sets, for the caller:
#   UNICORN_DIR   include/unicorn/unicorn.h and lib/libunicorn.a of the pinned wheel unicorn-2.1.4-cp37-abi3-macosx_11_0_arm64.whl (sha256 below).
#                 Taken from the environment, else ROOT/build/unicorn, else unpacked there from the wheel ($UNICORN_WHEEL, the wheel in ROOT, in
#                 the main checkout ~/prj/poly or in the pinned emu's cache ~/prj/poly/build/emu/cache), the sha256 is checked first
#   BRIDGE_AXF    the release axf the bridge maps (its loader, its libm, the device addresses of the windows). From the environment, else this
#                 tree's make original-release output platforms/mk66/build-trim/PolyendTracker.axf, built here when it is missing or when its
#                 stamp (build-trim/.bridge-axf-key) is not the key of this tree's firmware sources (bridge_axf_key below)
#                 (TOOLCHAIN, default agt/12.3.rel1 of this tree or of ~/prj/poly)
#   BRIDGE_ARGS   the make arguments of the build: BRIDGE=1 UNICORN_DIR=... BRIDGE_AXF=...
# bridge_env returns non zero (and says why on stderr) when one of them cannot be had. Unicorn is GPL-2.0: internal tool only.
UNICORN_WHEEL_NAME=unicorn-2.1.4-cp37-abi3-macosx_11_0_arm64.whl
UNICORN_WHEEL_SHA256=d6c93e0f60328d8f4a1792af3f834137a28050fcc2305f2ec01efe8558a9844e
# bridge_axf_key ROOT TOOLCHAIN_PREFIX: sha256 (24 hex) of the git index entries (mode, blob, path) of ROOT without the paths no image is built from,
# plus the toolchain version line. Empty (rc 1) when one of those paths has a local change or an untracked file. The excluded paths: the ui_shot
# tree, the test scripts of synth-open/tools (*_test.*, *_sim.sh, directly in that folder), run_tests.sh, run_smoke.sh, engines_para_check.sh,
# wt_pool.sh, jobs.sh (host only scripts), docs, ref and the *.md files, so a commit that only touches tests keeps the key. `git ls-files -s` takes the exclude pathspecs (ls-tree does not) and equals HEAD
# for a clean status of those paths
bridge_axf_key() {
    local root="$1" tc="$2" k ps=(. ':(exclude)synth-open/tools/ui_shot' ':(exclude)synth-open/tools/run_tests.sh'
        ':(exclude)synth-open/tools/engines_para_check.sh' ':(exclude,glob)synth-open/tools/*_test.*' ':(exclude,glob)synth-open/tools/*_sim.sh'
        ':(exclude)synth-open/tools/wt_pool.sh' ':(exclude)synth-open/tools/jobs.sh' ':(exclude)synth-open/tools/run_smoke.sh' ':(exclude)docs' ':(exclude)ref' ':(exclude,glob)**/*.md')
    [ -z "$(git -C "$root" status --porcelain -- "${ps[@]}" 2>/dev/null)" ] || return 1
    k="$(git -C "$root" ls-files -s -- "${ps[@]}" 2>/dev/null)" && [ -n "$k" ] || return 1
    { printf '%s\n' "$k"; "${tc}gcc" --version 2>/dev/null | head -1; } | shasum -a 256 | cut -c1-24
}
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
        tc="${TOOLCHAIN:-$root/agt/12.3.rel1/bin/arm-none-eabi-}"
        [ -x "${tc}gcc" ] || tc="$HOME/prj/poly/agt/12.3.rel1/bin/arm-none-eabi-"
        # The key of the firmware sources: the git index entries of the paths an image is built from (see bridge_axf_key) and the toolchain
        # version, empty when one of those paths has a local change or an untracked file (the tree is not a git state then). A local axf is
        # taken as is only when the stamp beside it (build-trim/.bridge-axf-key, written after the build below) names this key, so a checkout
        # to other firmware sources never maps an old axf. With no key (a dirty tree) an existing axf is used as it is, as before.
        local cache stamp key need="" lockd
        stamp="$root/platforms/mk66/build-trim/.bridge-axf-key"
        key="$(bridge_axf_key "$root" "$tc")"
        # the shared axf cache (RT_CACHE, default build/test-cache of the main checkout, RT_CACHE=off turns it off): a worktree on the same
        # firmware sources maps the same axf without its own make original-release
        cache="${RT_CACHE:-$(cd "$root" && cd "$(git rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)/build/test-cache}"
        [ "$cache" != /build/test-cache ] || cache=off
        if [ ! -f "$BRIDGE_AXF" ]; then need=1
        elif [ -n "$key" ] && [ "$(cat "$stamp" 2>/dev/null)" != "$key" ]; then need=1; echo "bridge_env: the release axf of this tree is not from these firmware sources (no matching $stamp)" >&2; fi
        if [ -n "$need" ]; then
            if [ "$cache" != off ] && [ -n "$key" ] && [ -f "$cache/bridge-axf/$key/PolyendTracker.axf" ]; then
                BRIDGE_AXF="$cache/bridge-axf/$key/PolyendTracker.axf"; echo "bridge_env: shared release axf $BRIDGE_AXF" >&2
            else
                # sims that start together all find the axf missing or stale: one builds it, the others wait here and then see its stamp
                # (a lock dir of the tree, a killed holder is taken over after 20 min)
                lockd="$root/build/.bridge-axf.lock"; mkdir -p "$root/build"
                until mkdir "$lockd" 2>/dev/null; do
                    [ -z "$(find "$lockd" -maxdepth 0 -mmin +20 2>/dev/null)" ] || { rm -rf "$lockd"; continue; }
                    sleep 1
                done
                if [ -f "$BRIDGE_AXF" ] && { [ -z "$key" ] || [ "$(cat "$stamp" 2>/dev/null)" = "$key" ]; }; then
                    :   # built by the holder of the lock while this call waited
                else
                    echo "bridge_env: no matching release axf yet, make original-release ($tc)" >&2
                    rm -f "$stamp"
                    make -s -C "$root" original-release TOOLCHAIN="$tc" -j"$(sh "$root/synth-open/tools/jobs.sh" n)" >"$root/build/bridge_env_original.log" 2>&1 \
                        || { tail -20 "$root/build/bridge_env_original.log" >&2; echo "bridge_env: make original-release failed (set BRIDGE_AXF)" >&2; rmdir "$lockd"; return 1; }
                    [ -z "$key" ] || [ ! -f "$BRIDGE_AXF" ] || printf '%s\n' "$key" > "$stamp"
                fi
                if [ "$cache" != off ] && [ -n "$key" ] && [ ! -e "$cache/bridge-axf/$key" ]; then   # store it: a private dir, then one rename
                    mkdir -p "$cache/bridge-axf" && tmp="$(mktemp -d "$cache/bridge-axf/.tmp.XXXXXX")" && cp "$BRIDGE_AXF" "$tmp/" &&
                        { [ -e "$cache/bridge-axf/$key" ] && rm -rf "$tmp" || mv "$tmp" "$cache/bridge-axf/$key"; }
                fi
                rmdir "$lockd" 2>/dev/null
            fi
        fi
    fi
    [ -f "$BRIDGE_AXF" ] || { echo "bridge_env: no release axf $BRIDGE_AXF" >&2; return 1; }
    export UNICORN_DIR BRIDGE_AXF
    BRIDGE_ARGS=(BRIDGE=1 UNICORN_DIR="$UNICORN_DIR" BRIDGE_AXF="$BRIDGE_AXF")
}
