#!/usr/bin/env bash
# Checks a release tag against the sources and prints its release notes.
#
#   scripts/release-notes.sh v0.1.0 > notes.md
#
# The tag must be v<major>.<minor>.<patch>[-<pre-release>], its version must be
# the one in CMakeLists.txt's project(), and CHANGELOG.md must have a section for
# it. A pre-release tag (v0.2.0-rc.1) may take its notes from [Unreleased] instead,
# since the release commit that dates the section comes after it.
set -euo pipefail

tag=${1:?usage: $0 v<major>.<minor>.<patch>[-<pre-release>]}
if [[ ! $tag =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-[0-9A-Za-z.-]+)?$ ]]; then
	echo "error: $tag is not v<major>.<minor>.<patch>[-<pre-release>]" >&2
	exit 1
fi
version=${BASH_REMATCH[1]}
pre_release=${BASH_REMATCH[2]}

root=$(cd "$(dirname "$0")/.." && pwd)
cmake_version=$(sed -nE 's/^project\(Sourcetrail VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
if [ "$cmake_version" != "$version" ]; then
	echo "error: $tag does not match project(Sourcetrail VERSION $cmake_version) in CMakeLists.txt" >&2
	exit 1
fi

# The lines under "## [<name>]" up to the next "## " heading.
section() {
	awk -v heading="## [$1]" 'index($0, heading) == 1 { on = 1; next } on && /^## / { exit } on' "$root/CHANGELOG.md"
}

notes=$(section "$version")
if [ -z "${notes//[[:space:]]/}" ] && [ -n "$pre_release" ]; then
	notes=$(section Unreleased)
fi
if [ -z "${notes//[[:space:]]/}" ]; then
	echo "error: CHANGELOG.md has no section for $version" >&2
	exit 1
fi
printf '%s\n' "$notes"
