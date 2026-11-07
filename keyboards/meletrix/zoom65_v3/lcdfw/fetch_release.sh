#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fetches the newest patch release of a zoom65v3-lcd-firmware minor version into a cache
# directory, and prints the paths of its first stage loader and firmware. A tag whose files
# aren't published is skipped, and the last download is reused if none can be reached.
# usage: fetch_release.sh <cache dir> <minor version, such as 0.1>
REPO=https://github.com/ozboar/zoom65v3-lcd-firmware
cache=$1
minor=$2
current="$cache/current-$minor"

paths() {
    echo "$cache/$1/zoom65v3-lcd-stage1-$1.bin $cache/$1/zoom65v3-lcd-firmware-$1.bin"
}

# Newest first, ignoring anything that isn't a plain patch release
patches() {
    GIT_TERMINAL_PROMPT=0 git ls-remote --tags --refs "$REPO.git" "v$minor.*" 2>/dev/null |
        sed -n "s|.*refs/tags/v$(echo "$minor" | sed 's/\./\\./g')\.\([0-9][0-9]*\)$|\1|p" |
        sort -rn
}

download() {
    tag=$1
    tmp="$cache/$tag.tmp"
    rm -rf "$tmp"
    mkdir -p "$tmp" || return 1
    for file in "zoom65v3-lcd-stage1-$tag.bin" "zoom65v3-lcd-firmware-$tag.bin" SHA256SUMS; do
        curl -fsSL -o "$tmp/$file" "$REPO/releases/download/$tag/$file" || return 1
    done
    # A release that doesn't match its checksums must stop the build, not fall back to an older one
    if ! (cd "$tmp" && sha256sum --quiet -c SHA256SUMS) >&2; then
        rm -rf "$tmp"
        echo "LCD firmware $tag does not match its checksums" >&2
        exit 1
    fi
    mv "$tmp" "$cache/$tag"
}

mkdir -p "$cache" || exit 1
for patch in $(patches); do
    tag="v$minor.$patch"
    if [ -d "$cache/$tag" ] || download "$tag"; then
        echo "$tag" > "$current"
        paths "$tag"
        exit 0
    fi
    rm -rf "$cache/$tag.tmp"
    echo "Could not download LCD firmware $tag, trying an older one" >&2
done

tag=$(cat "$current" 2>/dev/null)
if [ -z "$tag" ]; then
    echo "Could not get LCD firmware $minor from $REPO, and none was downloaded before" >&2
    exit 1
fi
echo "Could not get LCD firmware $minor from $REPO, using the downloaded $tag" >&2
paths "$tag"
