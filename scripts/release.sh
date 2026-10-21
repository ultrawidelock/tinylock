#!/bin/sh
# SPDX-FileCopyrightText: 2026 Aseem Chopra
# SPDX-License-Identifier: Apache-2.0

# Pin the libraries, or build a release from the pins.
#
#   scripts/release.sh lock
#   TINYLOCK_KEY=<signing key> scripts/release.sh build <version> [<earlier app.signed.hex>...]
#
# lock writes modules.lock: each library's checked-out commit, which must be
# clean and already on its origin/main. Commit it with the VERSION bump.
#
# build checks that this tree is clean and pushed, that door-lock/VERSION says
# <version> and that every library sits at its locked commit; builds the whole
# image from nothing, signed with TINYLOCK_KEY (MCUboot and tinyfota both trust
# it); runs tinyfota release against each earlier release's app image; leaves
# the four release files and NOTES.md in build/release/v<version>/; and tags
# v<version>, signed. The SDK is taken from NCS, by default /opt/nordic/ncs/v3.3.0.
#
# Keep every published *-app.signed.hex: a later update can only be built
# against it, and no build reproduces it byte for byte.

set -u

libs="tinytalk tinyble tinypsa tinygrid tinyfota tinyuwb tinycred"
ncs=v3.3.0
NCS=${NCS:-/opt/nordic/ncs/$ncs}
board=decawave_dwm3001cdk
here=$(cd "$(dirname "$0")/.." && pwd)
parent=$(dirname "$here")
lockfile="$here/modules.lock"

die() {
	echo "release: $*" >&2
	exit 1
}

usage() {
	echo "usage: $0 lock | build <version> [<earlier app.signed.hex>...]" >&2
	exit 2
}

# Fails unless a checkout is clean and its HEAD is already on origin/main.
published() {
	[ -z "$(git -C "$1" status --porcelain)" ] || die "$1 has local changes"
	git -C "$1" fetch -q origin || die "$1: fetch failed"
	git -C "$1" merge-base --is-ancestor HEAD origin/main || die "$1: HEAD is not on origin/main, push it first"
}

lock() {
	pins=
	for lib in $libs; do
		published "$parent/$lib"
		pins="$pins$lib $(git -C "$parent/$lib" rev-parse HEAD)
"
	done
	printf '%s' "$pins" > "$lockfile"
	cat "$lockfile"
	echo "wrote $lockfile: commit it with the VERSION bump" >&2
}

build() {
	version=$1
	shift
	echo "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || die "version must be major.minor.patch, not '$version'"
	file=$(awk -F' *= *' '/^VERSION_MAJOR/ { a = $2 } /^VERSION_MINOR/ { b = $2 } /^PATCHLEVEL/ { c = $2 } END { print a "." b "." c }' "$here/door-lock/VERSION")
	[ "$file" = "$version" ] || die "door-lock/VERSION says $file, not $version"
	key=${TINYLOCK_KEY:-}
	case "$key" in
	/*) [ -f "$key" ] || die "no signing key at $key" ;;
	*) die "set TINYLOCK_KEY to the absolute path of the image-signing key" ;;
	esac
	case "$(basename "$key")" in
	root-*) die "$key looks like one of MCUboot's published keys" ;;
	esac
	tag="v$version"
	git -C "$here" rev-parse -q --verify "refs/tags/$tag" >/dev/null && die "tag $tag exists"
	published "$here"
	[ -f "$lockfile" ] || die "no modules.lock: run '$0 lock' first"
	modules=
	for lib in $libs; do
		want=$(awk -v l="$lib" '$1 == l { print $2 }' "$lockfile")
		[ -n "$want" ] || die "$lib is not in modules.lock"
		have=$(git -C "$parent/$lib" rev-parse HEAD) || die "no checkout at $parent/$lib"
		[ "$have" = "$want" ] || die "$lib is at $have, modules.lock says $want: run ./bootstrap --locked"
		[ -z "$(git -C "$parent/$lib" status --porcelain)" ] || die "$lib has local changes"
		modules="$modules${modules:+;}$parent/$lib"
	done
	n=$#
	for base in "$@"; do
		[ -f "$base" ] || die "no earlier release image at $base"
		set -- "$@" --base "$base"
	done
	shift "$n"

	out="$here/build/release/$tag"
	b="$out/build"
	venv="$here/build/release/.venv"
	rm -rf "$out"
	mkdir -p "$out"
	if [ ! -x "$venv/bin/tinyfota" ]; then
		echo "installing the tinyfota host tool into $venv" >&2
		python3 -m venv "$venv" && "$venv/bin/pip" install -q "$parent/tinyfota/tools/tinyfota" ||
			die "could not install the tinyfota host tool"
	fi

	echo "building $tag, log in $out/build.log" >&2
	(cd "$NCS" && nrfutil sdk-manager toolchain launch --ncs-version "$ncs" -- \
		west build -b "$board" -d "$b" "$here/door-lock" -- \
		-DZEPHYR_EXTRA_MODULES="$modules" \
		-DCONF_FILE="prj.conf;stacks/tinyble.conf;stacks/tinygrid.conf;stacks/tinygrid-radio.conf;stacks/tinypsa.conf;stacks/tinyfota-tinyble.conf;stacks/tinyuwb.conf;stacks/tinycred.conf;overlay-release.conf" \
		-DEXTRA_DTC_OVERLAY_FILE="stacks/tinyuwb.overlay;stacks/tinyble.overlay" \
		-DSB_CONF_FILE=stacks/tinyfota/sysbuild.conf \
		-DPM_STATIC_YML_FILE=stacks/tinyfota/pm_static.yml \
		-Dmcuboot_CONF_FILE="$here/door-lock/stacks/tinyfota/mcuboot.conf" \
		-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="\"$key\"") >"$out/build.log" 2>&1 ||
		die "build failed, see $out/build.log"
	app="$b/door-lock/zephyr/zephyr.signed.hex"
	for f in "$b/merged.hex" "$app" "$b/dfu_application.zip"; do
		[ -f "$f" ] || die "the build left no $f"
	done
	code=$(sed -n 's/.*tinytalk manual pairing code: \([0-9-]*\).*/\1/p' "$out/build.log" | tail -1)
	[ -n "$code" ] || die "no pairing code in $out/build.log"

	echo "tinyfota release" >&2
	"$venv/bin/tinyfota" release --to "$app" "$@" --key "$key" --build-dir "$b" \
		--version "$version" --out-dir "$out/ota" --target "$board" --name DWM3001CDK \
		--soc nrf52833 >>"$out/build.log" 2>&1 || die "tinyfota release failed, see $out/build.log"

	cp "$b/merged.hex" "$out/tinylock-$tag-factory.hex"
	cp "$app" "$out/tinylock-$tag-app.signed.hex"
	cp "$b/dfu_application.zip" "$out/tinylock-$tag-dfu.zip"
	(cd "$out" && zip -qr "tinylock-$tag-ota.zip" ota) || die "could not zip $out/ota"
	# One line per paragraph: GitHub renders every newline in release notes.
	cat >"$out/NOTES.md" <<EOF
Matter door lock firmware for the DWM3001CDK (nRF52833): commissioned over BLE, run over Thread, unlocked by a phone over UWB.

### Flash

\`\`\`sh
nrfutil install device
nrfutil device program --firmware tinylock-$tag-factory.hex \\
  --options chip_erase_mode=ERASE_ALL
\`\`\`

Then add the lock in Apple Home with setup code \`$code\`. Home flags it as uncertified: it carries Matter's test attestation, and every board shares that code.

### Files

- **factory.hex**: a blank board, MCUboot and the app
- **dfu.zip**: an update over Bluetooth with nRF Device Manager
- **ota.zip**: tinyfota updates and the browser updater's index
- **app.signed.hex**: the app alone, the base of every later update

Built from $(git -C "$here" rev-parse --short HEAD) with the libraries in \`modules.lock\` and NCS $ncs. \`./bootstrap --locked\` at \`$tag\` rebuilds it.

Unofficial. Not affiliated with, endorsed or certified by any company or standards body named here. Trademarks belong to their owners.
EOF
	git -C "$here" tag -s "$tag" -m "tinylock $tag" || die "could not tag $tag"

	echo "release $tag ready in $out" >&2
	echo "  git -C $here push origin $tag" >&2
	echo "  gh release create $tag --draft --title 'tinylock $tag' --notes-file $out/NOTES.md $out/tinylock-$tag-*" >&2
}

case "${1:-}" in
lock)
	[ $# -eq 1 ] || usage
	lock
	;;
build)
	[ $# -ge 2 ] || usage
	shift
	build "$@"
	;;
*) usage ;;
esac
