#!/bin/sh

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${1:-"$project_root/build/linux-demo"}
output_dir=${2:-"$project_root/dist"}
package_name=SimNetLab-Demo-0.1.0-linux-x86_64

server="$build_dir/app/Server"
client="$build_dir/app/Client"

if [ ! -x "$server" ] || [ ! -x "$client" ]; then
    echo "Build the linux-demo preset before packaging:" >&2
    echo "  cmake --preset linux-demo" >&2
    echo "  cmake --build --preset linux-demo" >&2
    exit 1
fi

stage_parent=$(mktemp -d "${TMPDIR:-/tmp}/simnetlab-demo.XXXXXX")
stage="$stage_parent/$package_name"

cleanup()
{
    rm -rf -- "$stage_parent"
}
trap cleanup EXIT HUP INT TERM

mkdir -p \
    "$stage/bin" \
    "$stage/config" \
    "$stage/assets/render" \
    "$stage/licenses" \
    "$stage/logs" \
    "$output_dir"

cp "$server" "$stage/bin/Server"
cp "$client" "$stage/bin/Client"
cp "$project_root/packaging/linux-demo/run_demo.sh" "$stage/run_demo.sh"
cp "$project_root/packaging/linux-demo/README.txt" "$stage/README.txt"
cp "$project_root/packaging/linux-demo/config/server_demo.json" "$stage/config/server_demo.json"
cp "$project_root/packaging/linux-demo/config/client_demo.json" "$stage/config/client_demo.json"
cp "$project_root/config/shared_demo_visual.json" "$stage/config/shared_demo_visual.json"
cp "$project_root/assets/render/boid.obj" "$stage/assets/render/boid.obj"
cp \
    "$project_root/src/render/assets/JetBrainsMonoNerdFont-Regular.ttf" \
    "$stage/assets/render/JetBrainsMonoNerdFont-Regular.ttf"
cp \
    "$project_root/src/render/assets/JetBrainsMonoNerdFont-OFL.txt" \
    "$stage/licenses/JetBrainsMonoNerdFont-OFL.txt"
cp \
    "$project_root/packaging/linux-demo/licenses/GCC-GPL-3.0.txt" \
    "$stage/licenses/GCC-GPL-3.0.txt"
cp \
    "$project_root/packaging/linux-demo/licenses/GCC-RUNTIME-LIBRARY-EXCEPTION.txt" \
    "$stage/licenses/GCC-RUNTIME-LIBRARY-EXCEPTION.txt"

vcpkg_share="$build_dir/vcpkg_installed/x64-linux/share"
if [ ! -d "$vcpkg_share" ]; then
    echo "Missing vcpkg license metadata in $vcpkg_share" >&2
    exit 1
fi

for copyright_file in "$vcpkg_share"/*/copyright; do
    [ -f "$copyright_file" ] || continue
    dependency=$(basename "$(dirname "$copyright_file")")
    cp "$copyright_file" "$stage/licenses/${dependency}.txt"
done

commit=$(git -C "$project_root" rev-parse HEAD)
source_tag=$(git -C "$project_root" describe --tags --exact-match HEAD 2>/dev/null || echo untagged)
if [ -z "$(git -C "$project_root" status --porcelain --untracked-files=normal)" ]; then
    source_tree=clean
else
    source_tree=modified
fi

{
    echo "SimNetLab Demo 0.1.0"
    echo "source_repository=https://github.com/V4LKdev/SimNetLab"
    echo "source_commit=$commit"
    echo "source_tag=$source_tag"
    echo "source_tree=$source_tree"
    echo "architecture=x86_64"
    echo "server_sha256=$(sha256sum "$stage/bin/Server" | cut -d ' ' -f 1)"
    echo "client_sha256=$(sha256sum "$stage/bin/Client" | cut -d ' ' -f 1)"
} >"$stage/BUILD-INFO.txt"

archive="$output_dir/$package_name.tar.gz"
archive_basename=$(basename "$archive")
tar \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    -C "$stage_parent" \
    -czf "$archive" \
    "$package_name"
archive_hash=$(sha256sum "$archive" | cut -d ' ' -f 1)
printf '%s  %s\n' "$archive_hash" "$archive_basename" >"$archive.sha256"

echo "$archive"
echo "$archive.sha256"
