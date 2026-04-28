#!/bin/bash
# Release helper script - bumps version and creates tag
# TICKET_SF005: Ported from NexusFix, adapted for StratForge.
#
# Usage: ./scripts/release.sh [patch|minor|major] [--dry-run] [--no-tag]
#
# Options:
#   patch|minor|major  Version bump type (default: patch)
#   --dry-run          Show what would happen without making changes
#   --no-tag           Update version but don't create git tag (dev builds)
#
# Guidelines:
#   patch  - Bug fixes, internal refactoring (no API change)
#   minor  - New features, new public API (backward compatible)
#   major  - Breaking changes (1.0 = production ready)

set -e

# Parse arguments
BUMP_TYPE="patch"
DRY_RUN=false
NO_TAG=false

for arg in "$@"; do
    case "$arg" in
        patch|minor|major)
            BUMP_TYPE="$arg"
            ;;
        --dry-run)
            DRY_RUN=true
            ;;
        --no-tag)
            NO_TAG=true
            ;;
        -h|--help)
            head -15 "$0" | tail -14
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            exit 1
            ;;
    esac
done

# Get current version from CMakeLists.txt
CURRENT_VERSION=$(grep -oP 'project\(stratforge VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)

if [ -z "$CURRENT_VERSION" ]; then
    echo "Error: Could not find version in CMakeLists.txt"
    exit 1
fi

IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT_VERSION"

case "$BUMP_TYPE" in
    major)
        MAJOR=$((MAJOR + 1))
        MINOR=0
        PATCH=0
        ;;
    minor)
        MINOR=$((MINOR + 1))
        PATCH=0
        ;;
    patch)
        PATCH=$((PATCH + 1))
        ;;
esac

NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
TAG="v${NEW_VERSION}"

# Get last tag
LAST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "")

echo "========================================"
echo "  StratForge Release Helper"
echo "========================================"
echo ""
echo "Current version: $CURRENT_VERSION"
echo "New version:     $NEW_VERSION"
echo "Bump type:       $BUMP_TYPE"
if [ "$NO_TAG" = true ]; then
    echo "Tag:             (skipped)"
else
    echo "Tag:             $TAG"
fi
echo ""

# Show commits since last tag
echo "========================================"
echo "  Changes since ${LAST_TAG:-beginning}"
echo "========================================"
echo ""

if [ -n "$LAST_TAG" ]; then
    COMMIT_COUNT=$(git rev-list --count "${LAST_TAG}..HEAD")
    echo "Commits: $COMMIT_COUNT"
    echo ""
    git log --oneline "${LAST_TAG}..HEAD" | head -20
    if [ "$COMMIT_COUNT" -gt 20 ]; then
        echo "... and $((COMMIT_COUNT - 20)) more"
    fi
else
    git log --oneline | head -10
fi

echo ""

# Analyze commits for recommended bump type
echo "========================================"
echo "  Bump Type Analysis"
echo "========================================"
echo ""

if [ -n "$LAST_TAG" ]; then
    COMMITS=$(git log --oneline "${LAST_TAG}..HEAD")
else
    COMMITS=$(git log --oneline)
fi

HAS_BREAKING=$(echo "$COMMITS" | grep -iE "BREAKING|!:" || true)
HAS_FEATURE=$(echo "$COMMITS" | grep -iE "^[a-f0-9]+ (feat|add|implement)" || true)
HAS_FIX=$(echo "$COMMITS" | grep -iE "^[a-f0-9]+ (fix|bug|patch)" || true)

RECOMMENDED="patch"
if [ -n "$HAS_BREAKING" ]; then
    RECOMMENDED="major"
    echo "[!] Breaking changes detected - consider MAJOR bump"
elif [ -n "$HAS_FEATURE" ]; then
    RECOMMENDED="minor"
    echo "[+] New features detected - consider MINOR bump"
elif [ -n "$HAS_FIX" ]; then
    RECOMMENDED="patch"
    echo "[~] Bug fixes detected - PATCH is appropriate"
else
    echo "[?] Only internal changes - consider skipping release"
    echo "    Use --no-tag for dev builds without release"
fi

echo ""
echo "Selected: $BUMP_TYPE"
if [ "$BUMP_TYPE" != "$RECOMMENDED" ]; then
    echo "Warning: Recommended was '$RECOMMENDED'"
fi
echo ""

# Dry run - stop here
if [ "$DRY_RUN" = true ]; then
    echo "========================================"
    echo "  Dry Run - No changes made"
    echo "========================================"
    exit 0
fi

# Check if tag already exists
if [ "$NO_TAG" = false ] && git tag -l | grep -q "^${TAG}$"; then
    echo "Error: Tag $TAG already exists"
    exit 1
fi

# Confirm
echo "========================================"
echo "  Confirm Release"
echo "========================================"
echo ""

if [ "$NO_TAG" = true ]; then
    read -p "Update version to ${NEW_VERSION}? [y/N] " -n 1 -r
else
    read -p "Release ${TAG}? [y/N] " -n 1 -r
fi
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 0
fi

# Update CMakeLists.txt
sed -i "s/project(stratforge VERSION ${CURRENT_VERSION}/project(stratforge VERSION ${NEW_VERSION}/" CMakeLists.txt
echo "Updated CMakeLists.txt"

# Commit
git add CMakeLists.txt
git commit -m "Bump version to ${NEW_VERSION}"
echo "Committed version bump"

if [ "$NO_TAG" = true ]; then
    echo ""
    echo "Version updated to ${NEW_VERSION} (no tag)"
    echo "Use 'git push origin main' when ready"
else
    # Tag
    git tag -a "$TAG" -m "Release ${TAG}"
    echo ""
    echo "========================================"
    echo "  Tagged $TAG"
    echo "========================================"
    echo "Push with:"
    echo "  git push origin main && git push origin $TAG"
    echo ""
    echo "GitHub Actions will create the release automatically."
fi
