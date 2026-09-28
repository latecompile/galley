#!/bin/sh
# Galley's regression test.
#
# Assertions over the fixture book and isolated agent profiles:
#
#   1. `galley --check` reconciles md4c's parse against BlockScanner's
#      independent scan of the same bytes, per chapter. Its whole output is
#      compared to test/expected/check.txt, so a change in any chapter's block
#      count shows up even when the counts still agree with each other.
#   2. `galley --blocks` is compared per chapter. That pins the byte ranges a
#      comment anchors to, which the count comparison in (1) cannot see: two
#      readings can agree on how many blocks there are and still disagree
#      about where one of them starts.
#   3. A stand-in codex CLI pins model discovery, an append to an existing
#      agents.toml, duplicate detection, and argv expansion with optional
#      model and effort values present and absent.
#   4. CLI parser fixtures, config/cache failure cases and picker callbacks
#      arriving out of order, evaluated with Qt's JavaScript engine.
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

# Nothing is done here about the display. galley puts its own terminal modes
# on the offscreen platform, and this script running headless is what proves
# it still does.

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

# 3. Model profiles. Everything is redirected into the temporary XDG roots;
# the developer's real agents.toml and model cache are never read or written.
mkdir -p "$tmp/bin" "$tmp/config/galley" "$tmp/cache"
cat > "$tmp/bin/codex" <<'EOF'
#!/bin/sh
if [ "$1" != "debug" ] || [ "$2" != "models" ]; then
    echo "unexpected codex arguments: $*" >&2
    exit 2
fi
printf '%s\n' '{"models":[{"slug":"gpt-test","display_name":"GPT Test","visibility":"list","supported_reasoning_levels":[{"effort":"low"},{"effort":"high"}],"default_reasoning_level":"low"},{"slug":"gpt-hidden","display_name":"Hidden","visibility":"hide","supported_reasoning_levels":[],"default_reasoning_level":"low"}]}'
EOF
chmod +x "$tmp/bin/codex"

cat > "$tmp/config/galley/agents.toml" <<'EOF'
# Existing hand-written profile.
[claude]
model = "opus"
command = ["claude", "-p", "--model", "{model}", "{prompt}"]
EOF

model_out="$tmp/models.txt"
{
    env PATH="$tmp/bin:/usr/bin" XDG_CONFIG_HOME="$tmp/config" XDG_CACHE_HOME="$tmp/cache" \
        "$galley" --models codex | sed 's/refreshed .*/refreshed <time>/'
    sed -E 's/("refreshedAt": ")[^"]+"/\1<time>"/' \
        "$tmp/cache/galley/models.json"
    env PATH="$tmp/bin:/usr/bin" XDG_CONFIG_HOME="$tmp/config" XDG_CACHE_HOME="$tmp/cache" \
        "$galley" --add-model codex --model gpt-test --effort high
    env PATH="$tmp/bin:/usr/bin" XDG_CONFIG_HOME="$tmp/config" XDG_CACHE_HOME="$tmp/cache" \
        "$galley" --add-model codex --model gpt-test --effort high
    echo
    sed -E "s/(model picker, )[0-9]{4}-[0-9]{2}-[0-9]{2}/\\1<date>/" \
        "$tmp/config/galley/agents.toml"
    echo
    "$root/build/agent-profiles-test"
} > "$model_out" 2>&1
report "model profiles" "$expected/models.txt" "$model_out"

# Failure cases, every CLI parser and delayed picker callbacks. The helper
# creates its own temporary XDG roots and uses only the stand-in CLI above.
env PATH="$tmp/bin:/usr/bin" "$root/build/model-edges-test" "$root" \
    > "$tmp/model-edges.txt" 2>&1 || {
    cat "$tmp/model-edges.txt" >&2
    exit 1
}
report "model edge cases" "$expected/model-edges.txt" "$tmp/model-edges.txt"

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
