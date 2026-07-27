#!/usr/bin/env bash
# Packages a built RastaConverter into the zip layout the releases use:
#
#   RastaConverter<Version>-<platform>/
#       RastaConverter[.exe]   Palettes/   Generator/   GeneratorDual/
#       clacon2.ttf  help.txt  README.md  ChangeLog.md  test.jpg
#
# Used by the release workflow and by scripts/release.sh, so a package built on
# a laptop is the same package CI publishes. Runs on Linux, macOS and Windows
# (git-bash); zipping goes through `cmake -E tar` rather than a zip binary,
# because that is the one archiver every runner is guaranteed to have.
set -euo pipefail

usage() {
	echo "usage: $0 --build-dir DIR --platform NAME [--out DIR] [--version V]" >&2
	exit 2
}

build_dir=""
platform=""
out_dir="dist"
version=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--build-dir) build_dir=$2; shift 2 ;;
		--platform)  platform=$2;  shift 2 ;;
		--out)       out_dir=$2;   shift 2 ;;
		--version)   version=$2;   shift 2 ;;
		-h|--help)   usage ;;
		*) echo "unknown argument: $1" >&2; usage ;;
	esac
done

[[ -n "$build_dir" && -n "$platform" ]] || usage

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

# The version lives in one place; everything else reads it from there.
if [[ -z "$version" ]]; then
	version=$(sed -n 's/.*RASTA_CONVERTER_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/version.h)
fi
[[ -n "$version" ]] || { echo "could not read version from src/version.h" >&2; exit 1; }

# CMake puts the runtime layout beside the executable already, so the payload is
# whichever of those directories actually got built.
binary=""
for candidate in \
	"$build_dir/Release/RastaConverter.exe" \
	"$build_dir/Release/RastaConverter" \
	"$build_dir/RastaConverter.exe" \
	"$build_dir/RastaConverter"; do
	if [[ -f "$candidate" ]]; then binary="$candidate"; break; fi
done
[[ -n "$binary" ]] || { echo "no RastaConverter binary under $build_dir" >&2; exit 1; }
payload="$(dirname "$binary")"

# An absolute --out is left alone; a relative one is taken from the repository
# root, so the script behaves the same wherever it is invoked from.
case "$out_dir" in
	/*|[A-Za-z]:*) ;;
	*) out_dir="$root/$out_dir" ;;
esac

name="RastaConverter${version}-${platform}"
staging="$out_dir/$name"
rm -rf "$staging"
mkdir -p "$staging"

copy() { [[ -e "$1" ]] && cp -R "$1" "$staging/" || true; }

copy "$binary"
copy "$payload/Palettes"
copy "$payload/Generator"
copy "$payload/GeneratorDual"
copy "$payload/clacon2.ttf"
copy README.md
copy ChangeLog.md
copy help.txt
copy test.jpg
# Windows runtime libraries only exist next to a Windows build; on the other
# platforms there is nothing to copy and nothing to complain about.
for dll in "$payload"/*.dll; do
	[[ -e "$dll" ]] && cp "$dll" "$staging/" || true
done

# Executable bits survive the archive, which matters for the bundled assembler.
chmod +x "$staging/RastaConverter" 2>/dev/null || true
for mads in "$staging"/Generator*/mads*; do
	[[ -f "$mads" ]] && chmod +x "$mads" || true
done

archive="$out_dir/$name.zip"
rm -f "$archive"
(cd "$out_dir" && cmake -E tar cf "$name.zip" --format=zip "$name")

echo "$archive"
