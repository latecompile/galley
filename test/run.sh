#!/bin/sh
# Galley's regression test.
#
# Two assertions over the fixture book in test/book:
#
#   1. `galley --check` reconciles md4c's parse against BlockScanner's
#      independent scan of the same bytes, per chapter. Its whole output is
#      compared to test/expected/check.txt, so a change in any chapter's block
#      count shows up even when the counts still agree with each other.
#   2. `galley --blocks` is compared per chapter. That pins the byte ranges a
#      comment anchors to, which the count comparison in (1) cannot see: two
#      readings can agree on how many blocks there are and still disagree
#      about where one of them starts.
#
# Regenerate the expected output with UPDATE=1 after reading the diff and
# satisfying yourself it is an improvement.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
galley="$root/galley"
book="$root/test/book"
expected="$root/test/expected"
update="${UPDATE:-0}"

if [ ! -x "$galley" ]; then
    echo "test: no galley binary at $galley — run make first" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail=0

report() {
    # "$1" label, "$2" expected file, "$3" actual file
    if [ "$update" = "1" ]; then
        mkdir -p "$(dirname "$2")"
        cp "$3" "$2"
        echo "  updated  $1"
        return 0
    fi
    if [ ! -f "$2" ]; then
        echo "  MISSING  $1 — no expected output; run UPDATE=1 test/run.sh" >&2
        fail=1
        return 0
    fi
    if diff -u "$2" "$3" > "$tmp/diff"; then
        echo "  ok       $1"
    else
        echo "  FAIL     $1" >&2
        sed 's/^/           /' "$tmp/diff" >&2
        fail=1
    fi
}

# 1. Block mapping across the whole book.
status=0
"$galley" --check "$book" > "$tmp/check.txt" 2>&1 || status=$?
report "galley --check" "$expected/check.txt" "$tmp/check.txt"
if [ "$status" != "0" ]; then
    echo "test: galley --check exited $status — a chapter failed to render" >&2
    fail=1
fi

# 2. Block boundaries, chapter by chapter. The chapter list comes out of the
#    run above rather than from a second reading of the spine, so the two
#    assertions can never be made against different sets of files.
chapters=$(awk '$1 == "ok" || $1 == "UNMAP" { print $2 }' "$tmp/check.txt")
for rel in $chapters; do
    out="$tmp/blocks.txt"
    "$galley" --blocks "$rel" "$book" > "$out" 2>&1 || {
        echo "  FAIL     galley --blocks $rel — exited non-zero" >&2
        cat "$out" >&2
        fail=1
        continue
    }
    report "galley --blocks $rel" "$expected/blocks/$rel.txt" "$out"
done

if [ "$update" = "1" ]; then
    echo
    echo "expected output regenerated — read the diff before committing it"
    exit 0
fi

echo
if [ "$fail" = "0" ]; then
    echo "test: ok"
else
    echo "test: FAILED" >&2
fi
exit "$fail"
