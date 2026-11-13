#!/bin/bash

# Negative test runner for malformed archives.
#
# Each case is a directory test/testfiles/malformed_<name>/ containing:
#   testfile.<ext>  - a malformed archive built from scratch by test/data/malformed_test_generator.c
#   expect          - one line saying what munbox should do with it:
#                       error         munbox exits non-zero (reports an error)
#                       success       munbox exits zero (decodes, possibly ending a fork early)
#                       maxbytes:N    munbox exits zero and writes at most N bytes in total
#
# A case passes only if munbox finishes within a short timeout, is not killed by a signal, and the
# result matches expect. The timeout turns a decoder that never returns into a test failure instead
# of a hung CI job. These inputs make unpatched builds crash, abort or loop, so every case here is
# expected to fail before the hardening fixes and pass after them.

TEST_DIR="testfiles"
MUNBOX="../build/cmd/munbox"
OUTPUT_DIR="/tmp/munbox_negative_test"
TIMEOUT_SECS=15
VERBOSE=false

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

usage() {
    cat << EOF
Usage: $0 [options]

Options:
    --test-dir <dir>     Directory containing malformed_* test cases (default: testfiles)
    --munbox <path>      Path to munbox executable (default: ../build/cmd/munbox)
    --output-dir <dir>   Temporary directory for test outputs (default: /tmp/munbox_negative_test)
    --timeout <secs>     Per-case timeout in seconds (default: 15)
    --verbose            Enable verbose output
    --help               Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --test-dir) TEST_DIR="$2"; shift 2 ;;
        --munbox) MUNBOX="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --timeout) TIMEOUT_SECS="$2"; shift 2 ;;
        --verbose) VERBOSE=true; shift ;;
        --help) usage; exit 0 ;;
        *) echo "Unknown option: $1"; usage; exit 1 ;;
    esac
done

TEST_DIR=$(cd "$TEST_DIR" 2>/dev/null && pwd)
if [[ -z "$TEST_DIR" || ! -d "$TEST_DIR" ]]; then
    echo -e "${RED}Error: test directory not found${NC}"
    exit 1
fi
if [[ ! -x "$MUNBOX" ]]; then
    echo -e "${RED}Error: munbox executable not found or not executable: $MUNBOX${NC}"
    exit 1
fi

# Pick a timeout command. GNU coreutils provides 'timeout' (Linux CI); macOS has 'gtimeout' only when
# coreutils is installed. Fall back to a portable Perl alarm wrapper, which is available everywhere a
# shell and Perl are. run_with_timeout <secs> <cmd...> returns 124 if the command was timed out.
if command -v timeout >/dev/null 2>&1; then
    run_with_timeout() { local s=$1; shift; timeout -s KILL "$s" "$@"; }
elif command -v gtimeout >/dev/null 2>&1; then
    run_with_timeout() { local s=$1; shift; gtimeout -s KILL "$s" "$@"; }
else
    run_with_timeout() {
        local s=$1; shift
        perl -e 'my $s=shift; my $pid=fork; if($pid==0){exec @ARGV or exit 127}
                 $SIG{ALRM}=sub{kill "KILL",$pid; waitpid $pid,0; exit 124};
                 alarm $s; waitpid $pid,0; exit($?>>8 ? $?>>8 : ($? & 127 ? 128+($? & 127) : 0))' "$s" "$@"
    }
fi

# Total size in bytes of all regular files under a directory (portable: no find -printf)
total_bytes() {
    local sum=0 f
    while IFS= read -r f; do
        sum=$(( sum + $(wc -c < "$f") ))
    done < <(find "$1" -type f)
    echo "$sum"
}

mkdir -p "$OUTPUT_DIR"

passed=0
failed=0
failed_tests=()

for dir in "$TEST_DIR"/malformed_*; do
    [[ -d "$dir" ]] || continue
    testname=$(basename "$dir")
    [[ -f "$dir/expect" ]] || { echo -e "${RED}  FAIL: $testname - no expect file${NC}"; ((failed++)); continue; }

    input_file=""
    for file in "$dir"/testfile.*; do
        [[ -f "$file" ]] && input_file="$file"
    done
    if [[ -z "$input_file" ]]; then
        echo -e "${RED}  FAIL: $testname - no testfile${NC}"; ((failed++)); continue
    fi

    expect=$(head -n 1 "$dir/expect" | tr -d '[:space:]')
    out="$OUTPUT_DIR/$testname"
    rm -rf "$out"; mkdir -p "$out"

    run_with_timeout "$TIMEOUT_SECS" "$MUNBOX" -a -o "$out" "$input_file" > "$out/.stdout" 2>&1
    status=$?

    reason=""
    if [[ $status -eq 124 ]]; then
        reason="did not return within ${TIMEOUT_SECS}s"
    elif [[ $status -gt 128 ]]; then
        reason="killed by signal $((status - 128))"
    else
        case "$expect" in
            error)
                [[ $status -eq 0 ]] && reason="expected an error but it succeeded" ;;
            success)
                [[ $status -ne 0 ]] && reason="expected success but it exited $status" ;;
            maxbytes:*)
                limit=${expect#maxbytes:}
                if [[ $status -ne 0 ]]; then
                    reason="expected success but it exited $status"
                else
                    bytes=$(total_bytes "$out")
                    [[ $bytes -gt $limit ]] && reason="wrote $bytes bytes, more than the $limit-byte limit"
                fi ;;
            *)
                reason="unknown expect value '$expect'" ;;
        esac
    fi

    if [[ -z "$reason" ]]; then
        echo -e "${GREEN}  PASS: $testname ($expect)${NC}"
        ((passed++))
        rm -rf "$out"
    else
        echo -e "${RED}  FAIL: $testname ($expect) - $reason${NC}"
        [[ "$VERBOSE" = true ]] && { echo "---- munbox output ----"; cat "$out/.stdout"; echo "-----------------------"; }
        ((failed++))
        failed_tests+=("$testname: $reason")
    fi
done

total=$((passed + failed))
echo
echo "============================================================"
echo "NEGATIVE TEST SUMMARY: $passed/$total passed"
echo "============================================================"
if [[ $failed -gt 0 ]]; then
    for failure in "${failed_tests[@]}"; do
        echo "  $failure"
    done
    exit 1
fi
exit 0
