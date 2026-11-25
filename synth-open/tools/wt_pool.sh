#!/usr/bin/env bash
# A small pool of kept worktrees with warm build dirs: a new branch or a sha is checked out into a free one instead of a fresh
# `git worktree add` (no cold objects, no cold ui_shot flavours, no new 300 MB build dirs per task).
#
#   synth-open/tools/wt_pool.sh take REF [-b NEW_BRANCH]   lock a free clean slot, check REF out there, print its path (stdout, one line)
#   synth-open/tools/wt_pool.sh give PATH                  detach the slot's HEAD (its branch is free again) and unlock it
#   synth-open/tools/wt_pool.sh list                       every slot: HEAD, branch, lock owner and age, dirty or clean
#
#   REF: a local branch (switched to, it must not be checked out in another worktree), or any commit-ish (a detached HEAD at it).
#   -b NEW_BRANCH: `git switch -c NEW_BRANCH REF`. take never touches a dirty slot (tracked changes or untracked files): it takes the
#   next one and says so. A missing slot is made once with `git worktree add --detach`, the agt toolchain link of the main checkout is
#   added to a slot that has none.
#
# Build dirs after the switch (make tracks sources and headers through the .d files, not flags):
#   - ui_shot flavours (build/ui-shot*): self-healing, the Makefile's flags stamp (a hash of every compile flag) rebuilds a flavour whose
#     defines changed, so they are kept as they are.
#   - the firmware objects (core/build-*, platforms/*/build*): make does not track their flags, so when a Makefile, a *.mk or a linker
#     script differs between the old and the new HEAD they are wiped (the next make original-release builds them again, ccache helps).
#   - the release outputs (platforms/mk66/build-trim/PolyendTracker.axf, build/*.ptf, build/modit.plg): removed whenever a firmware source
#     differs, (bridge_env.sh since poly-test-speed-r4 also checks the stamp beside the axf against the key of the firmware sources, so this removal is a second guard and keeps the same exclude list as bridge_axf_key).
#
# Environment: WT_POOL_DIR (default <main checkout>-wt, ~/prj/poly-wt here: the internal disk. ~/prj/poly/.wt is a link to
# /Volumes/StoreCI and is refused, as is any pool that resolves there), WT_POOL_SIZE (default 3, slots
# pool-1..pool-N), WT_POOL_STALE_H (default 12: a lock older than that is taken over, with a note). Lock = the dir <slot>.lock with an
# owner file (task ORCH_TASK_ID, ref, epoch). A crashed holder never blocks the pool for more than WT_POOL_STALE_H hours.
set -uo pipefail
die() { echo "wt_pool: $*" >&2; exit 1; }
say() { echo "wt_pool: $*" >&2; }
main=$(cd "$(dirname "$0")" && cd "$(git rev-parse --git-common-dir)/.." && pwd -P) || die "not in a git checkout"
pool=${WT_POOL_DIR:-$main-wt}; size=${WT_POOL_SIZE:-3}; stale_h=${WT_POOL_STALE_H:-12}
mkdir -p "$pool" && pool=$(cd "$pool" && pwd -P) || die "no pool dir $pool"
case $pool in /Volumes/StoreCI/*) die "the pool must be on the internal disk, not $pool";; esac
[ -e "$pool/.metadata_never_index" ] || : > "$pool/.metadata_never_index"   # Spotlight stays out of the build dirs

clean() { [ -z "$(git -C "$1" status --porcelain --untracked-files=normal 2> /dev/null)" ]; }
age_h() { echo $(( ($(date +%s) - $(stat -f %m "$1" 2> /dev/null || stat -c %Y "$1")) / 3600 )); }
lock() {  # lock SLOT REF: 0 = this call owns the slot now
  local l=$1.lock
  if ! mkdir "$l" 2> /dev/null; then
    [ "$(age_h "$l")" -ge "$stale_h" ] || return 1
    say "taking over the stale lock of $(basename "$1") ($(cat "$l/owner" 2> /dev/null), $(age_h "$l") h old)"
    rm -rf "$l"; mkdir "$l" 2> /dev/null || return 1
  fi
  printf 'task=%s ref=%s taken=%s\n' "${ORCH_TASK_ID:-$USER}" "$2" "$(date +%s)" > "$l/owner"
}
unlock() { rm -rf "$1.lock"; }

# build dirs that a switch from OLD to NEW makes unsafe (see the header)
fix_builds() {  # fix_builds SLOT OLD NEW
  local s=$1 o=$2 n=$3
  [ "$o" = "$n" ] && return 0
  if [ -n "$(git -C "$s" diff --name-only "$o" "$n" -- ':(glob)**/Makefile*' ':(glob)**/*.mk' ':(glob)**/*.ld' 2> /dev/null | head -1)" ]; then
    say "build flags may differ ($(git -C "$s" diff --name-only "$o" "$n" -- ':(glob)**/Makefile*' ':(glob)**/*.mk' ':(glob)**/*.ld' | wc -l | tr -d ' ') Makefiles/*.mk/*.ld changed): wiping the firmware objects"
    rm -rf "$s"/core/build-* "$s"/platforms/*/build "$s"/platforms/*/build-*
  fi
  if [ -n "$(git -C "$s" diff --name-only "$o" "$n" -- . ':(exclude)synth-open/tools/ui_shot' ':(exclude)synth-open/tools/run_tests.sh' ':(exclude)synth-open/tools/engines_para_check.sh' ':(exclude,glob)synth-open/tools/*_test.*' ':(exclude,glob)synth-open/tools/*_sim.sh' ':(exclude)docs' ':(exclude)ref' ':(exclude,glob)**/*.md' 2> /dev/null | head -1)" ]; then
    rm -f "$s/platforms/mk66/build-trim/PolyendTracker.axf" "$s"/build/*.ptf "$s/build/modit.plg"
  fi
}

case ${1:-} in
  take)
    ref=${2:-}; [ -n "$ref" ] || die "take REF [-b NEW_BRANCH]"
    newb=""; [ "${3:-}" = -b ] && { newb=${4:-}; [ -n "$newb" ] || die "-b needs a branch name"; }
    sha=$(git -C "$main" rev-parse --verify --quiet "$ref^{commit}") || die "no commit $ref"
    isbranch=""; [ -z "$newb" ] && git -C "$main" show-ref --verify --quiet "refs/heads/$ref" && isbranch=1
    for i in $(seq 1 "$size"); do
      s=$pool/pool-$i
      lock "$s" "${newb:-$ref}" || continue
      if [ ! -e "$s/.git" ]; then
        say "making $s (git worktree add --detach, once)"
        git -C "$main" worktree add --detach "$s" "$sha" > /dev/null 2>&1 || { unlock "$s"; die "git worktree add $s failed"; }
      elif ! clean "$s"; then
        say "pool-$i is dirty (left by a holder), skipped: $(git -C "$s" status --porcelain --untracked-files=normal | head -3 | tr '\n' ' ')"
        unlock "$s"; continue
      fi
      old=$(git -C "$s" rev-parse HEAD)
      if [ -n "$newb" ]; then git -C "$s" switch -q -c "$newb" "$sha"
      elif [ -n "$isbranch" ]; then git -C "$s" switch -q "$ref"
      else git -C "$s" checkout -q --detach "$sha"; fi || { unlock "$s"; die "checkout of $ref in $s failed (a branch checked out in another worktree?)"; }
      fix_builds "$s" "$old" "$(git -C "$s" rev-parse HEAD)"
      [ -e "$s/agt" ] || [ ! -e "$main/agt" ] || ln -s "$(readlink "$main/agt" || echo "$main/agt")" "$s/agt"
      say "took pool-$i at $(git -C "$s" rev-parse --short HEAD) ($(git -C "$s" branch --show-current | grep . || echo detached)), give it back with: $0 give $s"
      echo "$s"; exit 0
    done
    die "no free clean slot of $size in $pool ($0 list)" ;;
  give)
    s=$(cd "${2:-.}" 2> /dev/null && pwd -P) || die "give PATH"
    case $s in "$pool"/pool-[0-9]*) ;; *) die "$s is not a pool slot of $pool";; esac
    [ -d "$s.lock" ] || die "$s is not taken"
    clean "$s" || die "$s has local changes, commit or drop them first (the next taker would skip a dirty slot): $(git -C "$s" status --porcelain | head -3 | tr '\n' ' ')"
    git -C "$s" checkout -q --detach || die "detach in $s failed"
    unlock "$s"; say "gave back $(basename "$s") at $(git -C "$s" rev-parse --short HEAD)" ;;
  list)
    for i in $(seq 1 "$size"); do
      s=$pool/pool-$i
      if [ ! -e "$s/.git" ]; then echo "pool-$i  (not made yet)"; continue; fi
      st=clean; clean "$s" || st=dirty
      lk=free; [ -d "$s.lock" ] && lk="taken $(cat "$s.lock/owner" 2> /dev/null | tr -d '\n') age $(age_h "$s.lock") h"
      echo "pool-$i  $(git -C "$s" rev-parse --short HEAD) $(git -C "$s" branch --show-current | grep . || echo detached)  $st  $lk"
    done ;;
  *) sed -n '2,/^set -uo/p' "$0" | sed '$d' >&2; exit 2 ;;
esac
