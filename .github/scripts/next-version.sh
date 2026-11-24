#!/usr/bin/env bash
# Computes the next version from the commit messages since the last
# vX.Y.Z tag (Conventional Commits), and writes the release notes.
#
#   feat!: … or "BREAKING CHANGE" in a body -> major (X+1.0.0)
#   feat: …                                  -> minor (X.Y+1.0)
#   anything else (fix:, perf:, …)           -> patch (X.Y.Z+1)
#   no tag yet                               -> 0.1.0
#
# Usage: next-version.sh [notes-file]
# Prints the version (without "v"); prints nothing when there is no new
# commit since the last tag.
set -euo pipefail

notes=${1:-/dev/null}
last=$(git describe --tags --abbrev=0 --match 'v[0-9]*.[0-9]*.[0-9]*' 2>/dev/null || true)

if [[ -z "$last" ]]; then
    version=0.1.0
    range=HEAD
else
    range="$last..HEAD"
    [[ -z "$(git rev-list --no-merges "$range")" ]] && exit 0
    IFS=. read -r major minor patch <<<"${last#v}"
    subjects=$(git log --no-merges --format=%s "$range")
    bodies=$(git log --no-merges --format=%b "$range")
    if grep -qE '^[a-z]+(\([^)]*\))?!:' <<<"$subjects" || grep -q 'BREAKING CHANGE' <<<"$bodies"; then
        version="$((major + 1)).0.0"
    elif grep -qE '^feat(\([^)]*\))?:' <<<"$subjects"; then
        version="$major.$((minor + 1)).0"
    else
        version="$major.$minor.$((patch + 1))"
    fi
fi

# Release notes: commits grouped by type, "type: " prefix removed
section() {
    local title=$1 pattern=$2 lines
    lines=$(git log --no-merges --format=%s "$range" | grep -E "$pattern" | sed -E 's/^[a-z]+(\([^)]*\))?!?: */- /' || true)
    [[ -n "$lines" ]] && printf '### %s\n%s\n\n' "$title" "$lines"
    return 0
}
{
    section "New features" '^feat(\([^)]*\))?!?:'
    section "Fixes" '^fix(\([^)]*\))?!?:'
    section "Other changes" '^(perf|refactor|test|docs|build|ci|chore|style)(\([^)]*\))?!?:'
    [[ -n "$last" ]] && echo "**Changes since $last**"
} >"$notes"

echo "$version"
