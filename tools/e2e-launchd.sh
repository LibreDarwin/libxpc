#!/bin/sh
#
# tools/e2e-launchd.sh — end-to-end test for the xnuports launchd stub.
#
# Model: one shell invocation per assertion.  launchd_stub creates a local
# Mach port, runs the launchctl command in-process (a serve thread answers),
# and exits with launchctl's status.  So every line below is:
#
#     launchd_stub [--env K=V]... -- launchctl <cmd>
#
# Exit status: 0 = all assertions pass, 1 = any failure.

set -u
cd "$(dirname "$0")/.."

ROOT="$(pwd)"
RELEASE="$ROOT/build/release"
STUB="$RELEASE/launchd_stub"
LAUNCHCTL="$RELEASE/launchctl"

failures=0
passes=0

check() {
    # check <name> <expected-status> <command...>
    name="$1"; want="$2"; shift 2
    "$@" >/dev/null 2>&1
    got=$?
    if [ "$got" -eq "$want" ]; then
        passes=$((passes + 1))
    else
        failures=$((failures + 1))
        echo "FAIL: $name (want exit $want, got $got)"
    fi
}

expect_output() {
    # expect_output <name> <needle> <command...>
    name="$1"; needle="$2"; shift 2
    out=$("$@" 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        case "$out" in
            *"$needle"*)
                passes=$((passes + 1))
                ;;
            *)
                failures=$((failures + 1))
                echo "FAIL: $name — no '$needle' in output:"
                printf '%s\n' "$out" | sed 's/^/    /'
                ;;
        esac
    else
        failures=$((failures + 1))
        echo "FAIL: $name — command exited $rc"
        printf '%s\n' "$out" | sed 's/^/    /'
    fi
}

expect_empty() {
    # expect_empty <name> <command...> — exit 0 AND no output.
    name="$1"; shift
    out=$("$@" 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        if [ -z "$out" ]; then
            passes=$((passes + 1))
        else
            failures=$((failures + 1))
            echo "FAIL: $name — expected empty output, got:"
            printf '%s\n' "$out" | sed 's/^/    /'
        fi
    else
        failures=$((failures + 1))
        echo "FAIL: $name — expected exit 0, got $rc"
    fi
}

# --- wire plumbing -----------------------------------------------------

# version: pipe setup + PRINT routine request/reply + shmem wire round-trip.
# The stub's PRINT handler maps the caller's memory entry and writes its
# banner back into the region; version_cmd reads it there.  Pin the full
# string so a shmem-map regression (empty region) fails this test.
expect_output "version" "Darwin Bootstrapper Version 7.0.0: xnuports-stub launchd" \
    "$STUB" --launchctl "$LAUNCHCTL" -- version

# help is a local command: usage header, exit 0
expect_output "help" "Usage: launchctl" "$STUB" --launchctl "$LAUNCHCTL" -- help

# --- domain env --------------------------------------------------------

# getenv of the canned value (main() presets XNUXPORTS_TEST_VAR)
expect_output "env get canned" "hello-from-stub" \
    "$STUB" --launchctl "$LAUNCHCTL" -- getenv XNUXPORTS_TEST_VAR

# missing var: silent success, empty output (launchctl(1) behavior)
expect_empty "env get missing" \
    "$STUB" --launchctl "$LAUNCHCTL" -- getenv XNUXPORTS_DOES_NOT_EXIST

# --env preset round-trips through the wire (same set_domain_env path the
# SETENV routine handler uses)
expect_output "env override" "fresh-value" \
    "$STUB" --launchctl "$LAUNCHCTL" \
    --env XNUXPORTS_TEST_VAR=fresh-value -- getenv XNUXPORTS_TEST_VAR

# --- service table -----------------------------------------------------

# whole-table list
expect_output "list" "com.xnuports.stub.running" \
    "$STUB" --launchctl "$LAUNCHCTL" -- list
expect_output "list pid 502" "502" \
    "$STUB" --launchctl "$LAUNCHCTL" -- list

# per-service lookup
expect_output "list service" "502" \
    "$STUB" --launchctl "$LAUNCHCTL" -- list com.xnuports.stub.running

# nonexistent service -> SERVICE_NOT_FOUND -> launchd error code (113),
# matching launchctl(1) on a real system
check "list missing service" 113 \
    "$STUB" --launchctl "$LAUNCHCTL" -- list com.xnuports.stub.nope

# --- state mutation ----------------------------------------------------

# enable with a proper domain/service target round-trips and succeeds
# (bare labels are rejected by launchctl(1): "Unrecognized target specifier.",
# exit 64)
check "enable bare label" 64 \
    "$STUB" --launchctl "$LAUNCHCTL" -- enable com.xnuports.stub.running
check "enable" 0 \
    "$STUB" --launchctl "$LAUNCHCTL" -- enable user/$(id -u)/com.xnuports.stub.running

# disable shares the handler but must actually flip the state flag
check "disable" 0 \
    "$STUB" --launchctl "$LAUNCHCTL" -- disable user/$(id -u)/com.xnuports.stub.running

# --- service kickstart --------------------------------------------------

# running service: EALREADY + the running pid, delivered together (the
# reply-with-error path), printed with -p, exit 0
expect_output "kickstart running pid" "502" \
    "$STUB" --launchctl "$LAUNCHCTL" -- \
    kickstart -p user/$(id -u)/com.xnuports.stub.running
check "kickstart running" 0 \
    "$STUB" --launchctl "$LAUNCHCTL" -- \
    kickstart user/$(id -u)/com.xnuports.stub.running

# stopped service: fresh pid (777), exit 0
expect_output "kickstart stopped pid" "777" \
    "$STUB" --launchctl "$LAUNCHCTL" -- \
    kickstart -p user/$(id -u)/com.xnuports.stub.dead

# unknown service -> SERVICE_NOT_FOUND, message + 113 match launchctl(1)
check "kickstart missing" 113 \
    "$STUB" --launchctl "$LAUNCHCTL" -- \
    kickstart user/$(id -u)/com.xnuports.stub.nope

# --- bootout ------------------------------------------------------------

# service target form round-trips and succeeds silently
check "bootout service" 0 \
    "$STUB" --launchctl "$LAUNCHCTL" -- \
    bootout user/$(id -u)/com.xnuports.stub.running

# unknown service -> ESRCH, same message and status as launchctl(1)
check "bootout missing" 3 \
    "$STUB" --launchctl "$LAUNCHCTL" -- \
    bootout user/$(id -u)/com.xnuports.stub.nope

# --- bootstrap ----------------------------------------------------------

# unreadable plist path -> EIO, message + 5 match launchctl(1)
check "bootstrap missing" 5 \
    "$STUB" --launchctl "$LAUNCHCTL" -- \
    bootstrap user/$(id -u) /tmp/xnuports-no-such.plist

# readable but invalid plist -> same EIO failure as real launchd
printf 'this is not plist' > /tmp/xnuports-bad.plist
check "bootstrap invalid" 5 \
    "$STUB" --launchctl "$LAUNCHCTL" -- \
    bootstrap user/$(id -u) /tmp/xnuports-bad.plist
rm -f /tmp/xnuports-bad.plist

# --- kill ---------------------------------------------------------------

# running service: signal accepted, exit 0
check "kill running" 0 \
    "$STUB" --launchctl "$LAUNCHCTL" -- kill 9 \
    user/$(id -u)/com.xnuports.stub.running

# unknown service -> SERVICE_NOT_FOUND, message + 113 match launchctl(1)
check "kill missing" 113 \
    "$STUB" --launchctl "$LAUNCHCTL" -- kill 9 \
    user/$(id -u)/com.xnuports.stub.nope

# --- summary -----------------------------------------------------------
echo "launchd e2e: $passes passed, $failures failed"
[ "$failures" -eq 0 ]