#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fetches the latest zoom65v3-lcd-firmware release into a cache directory, and prints the paths
# of its first stage loader and firmware. Reuses the last download if the release can't be
# reached. usage: fetch_release.sh <cache dir>
REPO=https://github.com/ozboar/zoom65v3-lcd-firmware
cache=$1

paths() {
    echo "$cache/$1/zoom65v3-lcd-stage1-$1.bin $cache/$1/zoom65v3-lcd-firmware-$1.bin"
}

download() {
    tag=$1
    tmp="$cache/$tag.tmp"
    rm -rf "$tmp"
    mkdir -p "$tmp" || return 1
    for file in "zoom65v3-lcd-stage1-$tag.bin" "zoom65v3-lcd-firmware-$tag.bin" SHA256SUMS; do
        curl -fsSL -o "$tmp/$file" "$REPO/releases/download/$tag/$file" || return 1
    done
    (cd "$tmp" && sha256sum --quiet -c SHA256SUMS) >&2 || return 1
    mv "$tmp" "$cache/$tag"
}

mkdir -p "$cache" || exit 1
url=$(curl -fsSL -o /dev/null -w '%{url_effective}' "$REPO/releases/latest" 2>/dev/null)
tag=${url##*/}

case "$tag" in
    v*)
        if [ ! -d "$cache/$tag" ] && ! download "$tag"; then
            rm -rf "$cache/$tag.tmp"
            echo "Could not download or verify LCD firmware $tag" >&2
            exit 1
        fi
        echo "$tag" > "$cache/current"
        ;;
    *)
        tag=$(cat "$cache/current" 2>/dev/null)
        if [ -z "$tag" ]; then
            echo "Could not reach $REPO, and no LCD firmware was downloaded before" >&2
            exit 1
        fi
        echo "Could not reach $REPO, using the downloaded LCD firmware $tag" >&2
        ;;
esac
paths "$tag"
