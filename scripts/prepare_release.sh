#!/usr/bin/env bash
# prepare_release.sh — Generate a pre-filled release checklist for a given version
# Usage: scripts/prepare_release.sh v0.2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMPLATE="$ROOT_DIR/.internal/templates/RELEASE_CHECKLIST_TEMPLATE.md"
RELEASES_DIR="$ROOT_DIR/.internal/releases"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <version> (e.g., v0.2.0)"
    exit 1
fi

VERSION="$1"

if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: Version must match vX.Y.Z format (got: $VERSION)"
    exit 1
fi

if [[ ! -f "$TEMPLATE" ]]; then
    echo "Error: Template not found at $TEMPLATE"
    exit 1
fi

mkdir -p "$RELEASES_DIR"
OUTPUT="$RELEASES_DIR/${VERSION}_checklist.md"

if [[ -f "$OUTPUT" ]]; then
    echo "Error: Checklist already exists at $OUTPUT"
    echo "  Remove it first if you want to regenerate."
    exit 1
fi

# Copy template
cp "$TEMPLATE" "$OUTPUT"

# Replace version placeholder
sed -i "s/vX\.Y\.Z/$VERSION/g" "$OUTPUT"

# Replace date placeholder
TODAY=$(date +%Y-%m-%d)
sed -i "s/YYYY-MM-DD/$TODAY/g" "$OUTPUT"

# Auto-populate test counts if test binary exists
TEST_BIN="$ROOT_DIR/build-gcc/bin/tests/stratforge_tests"
if [[ -x "$TEST_BIN" ]]; then
    # Get test/assertion counts from Catch2
    TEST_OUTPUT=$("$TEST_BIN" --list-tests 2>/dev/null | tail -1 || echo "")
    if [[ "$TEST_OUTPUT" =~ ([0-9]+)\ test\ case ]]; then
        TEST_COUNT="${BASH_REMATCH[1]}"
        sed -i "s/Test count: ___ test cases/Test count: $TEST_COUNT test cases/" "$OUTPUT"
    fi
fi

# Auto-populate binary size
if [[ -x "$TEST_BIN" ]]; then
    SIZE_MB=$(du -m "$TEST_BIN" | cut -f1)
    sed -i "s/Test binary size: ___ MB/Test binary size: ${SIZE_MB} MB/" "$OUTPUT"
fi

# Run compliance check and note result
if [[ -x "$ROOT_DIR/tools/compliance_check.sh" ]]; then
    if "$ROOT_DIR/tools/compliance_check.sh" > /dev/null 2>&1; then
        sed -i 's/\[ \] `tools\/compliance_check.sh` PASSED/[x] `tools\/compliance_check.sh` PASSED/' "$OUTPUT"
    fi
fi

# Run performance regression check
if [[ -x "$ROOT_DIR/scripts/check_perf_regression.sh" ]]; then
    if "$ROOT_DIR/scripts/check_perf_regression.sh" > /dev/null 2>&1; then
        sed -i 's/\[ \] `scripts\/check_perf_regression.sh` PASSED/[x] `scripts\/check_perf_regression.sh` PASSED/' "$OUTPUT"
    fi
fi

echo "Release checklist created: $OUTPUT"
echo ""
echo "Next steps:"
echo "  1. Review and complete all sections in $OUTPUT"
echo "  2. Run full test suite: ./build-gcc/bin/tests/stratforge_tests"
echo "  3. Run sanitizer builds (ASan + UBSan)"
echo "  4. Get reviewer sign-off"
echo "  5. Tag release: git tag -a $VERSION -m 'Release $VERSION'"

# Open in editor if EDITOR is set
if [[ -n "${EDITOR:-}" ]]; then
    "$EDITOR" "$OUTPUT"
fi
