#!/bin/bash
# post-receive-ci.sh — CI hook for the Klawed bare repo on registry.kasafox.com
#
# INSTALL (on the registry server):
#   cp scripts/post-receive-ci.sh /opt/git/klawed.git/hooks/post-receive
#   chmod +x /opt/git/klawed.git/hooks/post-receive
#
# WHAT IT DOES:
#   On every push to master, clones the repo to a temp directory,
#   runs `make test && make comprehensive-scan`, and if both pass,
#   bumps the patch version and pushes back.  Pushes produced by
#   bump-patch (commit message starts with "chore: bump version")
#   are detected and skipped to avoid infinite recursion.

set -euxo pipefail

# ── logging ──────────────────────────────────────────────────────────
LOG_DIR="${KLAWED_CI_LOG_DIR:-/var/log}"
LOG_FILE="$LOG_DIR/klawed-ci.log"
mkdir -p "$LOG_DIR"
exec >> "$LOG_FILE" 2>&1
echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "CI hook triggered — $(date -u +%Y-%m-%dT%H:%M:%SZ)"

# ── configuration ────────────────────────────────────────────────────
# Only trigger CI for these branches (space separated, default: master)
CI_BRANCHES="${KLAWED_CI_BRANCHES:-master}"
# Timeout for each make target in seconds (0 = none)
MAKE_TIMEOUT="${KLAWED_CI_MAKE_TIMEOUT:-1800}"

# ── helper: is this a qualifying branch? ─────────────────────────────
is_ci_branch() {
    local branch="$1"
    for b in $CI_BRANCHES; do
        [ "$branch" = "$b" ] && return 0
    done
    return 1
}

# ── process each ref update ─────────────────────────────────────────
while read oldrev newrev refname; do
    echo "  oldrev:  $oldrev"
    echo "  newrev:  $newrev"
    echo "  refname: $refname"

    # ── extract branch name ──────────────────────────────────────
    BRANCH=$(echo "$refname" | sed 's|^refs/heads/||')
    if ! is_ci_branch "$BRANCH"; then
        echo "  → skipping (branch '$BRANCH' not in CI_BRANCHES: $CI_BRANCHES)"
        continue
    fi

    # ── detect bump-patch recursion ──────────────────────────────
    COMMIT_MSG=$(git log -1 --format=%s "$newrev" 2>/dev/null || true)
    if echo "$COMMIT_MSG" | grep -q "^chore: bump version"; then
        echo "  → skipping (version-bump commit, avoiding recursion)"
        continue
    fi

    # ── deleted branch guard ─────────────────────────────────────
    if [ "$newrev" = "0000000000000000000000000000000000000000" ]; then
        echo "  → skipping (branch deletion)"
        continue
    fi

    echo "  → starting CI for branch '$BRANCH' ($COMMIT_MSG)"

    # ── create temp working directory ────────────────────────────
    WORK_DIR=$(mktemp -d /tmp/klawed-ci-XXXXXX)
    echo "  work dir: $WORK_DIR"

    cleanup() {
        echo "  cleaning up $WORK_DIR"
        rm -rf "$WORK_DIR"
    }
    trap cleanup EXIT

    # ── clone the bare repo ──────────────────────────────────────
    # We are running inside the bare repo (hooks/post-receive),
    # so '.' (pwd) is the git directory itself.
    BARE_REPO_PATH="$(pwd)"
    echo "  cloning $BARE_REPO_PATH → $WORK_DIR …"
    git clone "$BARE_REPO_PATH" "$WORK_DIR"
    cd "$WORK_DIR"
    git checkout -q "$BRANCH"

    # ── resolve make command ─────────────────────────────────────
    # Some Linux systems have gmake; macOS has make/gmake from Homebrew.
    MAKE="make"
    if command -v gmake &>/dev/null; then MAKE="gmake"; fi

    # ── run tests ────────────────────────────────────────────────
    echo "  → make test"
    if [ "$MAKE_TIMEOUT" -gt 0 ]; then
        if ! timeout "$MAKE_TIMEOUT" "$MAKE" test; then
            echo "  ✗ TESTS FAILED (exit=$?)"
            exit 1
        fi
    else
        if ! "$MAKE" test; then
            echo "  ✗ TESTS FAILED (exit=$?)"
            exit 1
        fi
    fi
    echo "  ✓ make test passed"

    # ── run comprehensive scan ───────────────────────────────────
    echo "  → make comprehensive-scan"
    if [ "$MAKE_TIMEOUT" -gt 0 ]; then
        if ! timeout "$MAKE_TIMEOUT" "$MAKE" comprehensive-scan; then
            echo "  ✗ COMPREHENSIVE SCAN FAILED (exit=$?)"
            exit 1
        fi
    else
        if ! "$MAKE" comprehensive-scan; then
            echo "  ✗ COMPREHENSIVE SCAN FAILED (exit=$?)"
            exit 1
        fi
    fi
    echo "  ✓ make comprehensive-scan passed"

    # ── bump patch version and push ──────────────────────────────
    echo "  → make bump-patch"
    if ! "$MAKE" bump-patch; then
        echo "  ✗ BUMP-PATCH FAILED (exit=$?)"
        exit 1
    fi
    echo "  ✓ bump-patch completed"

    echo "  ✓ CI PASSED — version bumped and pushed"
    echo "══════════════════════════════════════════════════════════════════"
done

exit 0
# CI trigger test 1781984542
