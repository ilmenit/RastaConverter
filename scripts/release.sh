#!/usr/bin/env bash
# Cuts a release.
#
#   ./scripts/release.sh 1.0-RC2            # tag, push, wait for CI, report
#   ./scripts/release.sh 1.0-RC2 --dry-run  # say what it would do, change nothing
#   ./scripts/release.sh --local           # build and package here, publish nothing
#
# The build itself happens in GitHub Actions, on the three platforms it targets;
# this script only decides that a release is ready and starts it. Anything it
# checks - clean tree, tests, a ChangeLog section - is something that has gone
# wrong in a previous release somewhere, not ceremony.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

version=""
dry_run=0
local_only=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		--dry-run) dry_run=1; shift ;;
		--local)   local_only=1; shift ;;
		-h|--help)
			sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
			exit 0 ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) version=$1; shift ;;
	esac
done

file_version=$(sed -n 's/.*RASTA_CONVERTER_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/version.h)

say()  { printf '  %s\n' "$1"; }
fail() { printf 'error: %s\n' "$1" >&2; exit 1; }

# ---- build and package locally, for checking a package before releasing ------
if [[ $local_only -eq 1 ]]; then
	platform="linux_x64"
	case "$(uname -s)" in
		Darwin) platform="macos_$(uname -m)" ;;
		MINGW*|MSYS*|CYGWIN*) platform="win_x64" ;;
	esac
	say "building ${file_version} for ${platform}"
	cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release > /dev/null
	cmake --build build/release --config Release --parallel
	(cd build/release && ctest --output-on-failure)
	"$root/scripts/package.sh" --build-dir build/release --platform "$platform"
	exit 0
fi

[[ -n "$version" ]] || fail "which version? e.g. $0 1.0-RC2"

echo "Releasing ${version}:"

# ---- the checks --------------------------------------------------------------
[[ "$version" == "$file_version" ]] \
	|| fail "src/version.h says ${file_version}, not ${version}. Bump it first - it is what the .opt headers record."

grep -q "^RastaConverter${version} " ChangeLog.md \
	|| fail "ChangeLog.md has no 'RastaConverter${version}' section; the release notes are taken from it."

[[ -z "$(git status --porcelain)" ]] \
	|| fail "working tree is not clean; commit or stash first."

branch=$(git rev-parse --abbrev-ref HEAD)
[[ "$branch" == "master" ]] \
	|| fail "on branch ${branch}; releases are cut from master."

git fetch --quiet origin
[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/master)" ]] \
	|| fail "master and origin/master differ; push or pull first."

if git rev-parse "$version" >/dev/null 2>&1; then
	fail "tag ${version} already exists. Delete it first if you mean to replace the release."
fi

command -v gh > /dev/null || fail "gh (GitHub CLI) is required to watch the release."
gh auth status > /dev/null 2>&1 || fail "gh is not logged in; run 'gh auth login'."

say "version, ChangeLog, clean tree, master, no existing tag - all fine"

if [[ $dry_run -eq 1 ]]; then
	say "dry run: would tag ${version} and push it, which starts the release build"
	exit 0
fi

# ---- go ----------------------------------------------------------------------
say "tagging and pushing"
git tag -a "$version" -m "RastaConverter${version}"
git push origin "$version"

say "the Build workflow now builds Linux, macOS and Windows and publishes the release"
sleep 5
run_id=$(gh run list --workflow=build.yml --event=push --limit 10 \
	--json databaseId,headBranch --jq "map(select(.headBranch == \"${version}\")) | .[0].databaseId")

if [[ -z "$run_id" || "$run_id" == "null" ]]; then
	say "could not find the run yet; watch it with: gh run watch --workflow=build.yml"
	exit 0
fi

say "watching run ${run_id} (Ctrl+C is safe - the build keeps going)"
gh run watch "$run_id" --exit-status || fail "the build failed; the release was not published. Fix, delete the tag with 'git push --delete origin ${version}', and run this again."

echo
say "published: $(gh release view "$version" --json url --jq .url)"
