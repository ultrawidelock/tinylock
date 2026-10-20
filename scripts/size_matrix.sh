#!/bin/sh
# SPDX-FileCopyrightText: 2026 Aseem Chopra
# SPDX-License-Identifier: Apache-2.0

# Build the door-lock once per fragment set and print a Markdown table of
# flash and RAM.
#
#   TINYLOCK_MODULES="<tinytalk>;<other modules>" \
#     scripts/size_matrix.sh <build-root> <set>...
#
# A set is a '+'-joined list of fragment names. A name is a file in
# door-lock/stacks/ or door-lock/ without its .conf suffix; "stock" stands
# for bt-host+openthread+nrf-security. prj.conf always comes first. Example:
#   scripts/size_matrix.sh <build-root> stock stock+overlay-release
#
# west and the toolchain come from the caller's PATH (for example inside
# `nrfutil sdk-manager toolchain launch`, from the SDK workspace). Module
# paths come from TINYLOCK_MODULES only. BOARD overrides the board. Each
# build goes to <build-root>/<set>, its log to <build-root>/<set>.log.

set -u

usage() {
	echo "usage: TINYLOCK_MODULES=<modules> $0 <build-root> <set>..." >&2
	exit 2
}

[ $# -ge 2 ] || usage
[ -n "${TINYLOCK_MODULES:-}" ] || { echo "TINYLOCK_MODULES is not set" >&2; usage; }

root=$1
shift
app=$(cd "$(dirname "$0")/../door-lock" && pwd)
board=${BOARD:-decawave_dwm3001cdk}
mkdir -p "$root"
root=$(cd "$root" && pwd)

# 298312 -> 298,312
commas() {
	echo "$1" | awk '{ s = $0; o = ""; while (length(s) > 3) { o = "," substr(s, length(s) - 2) o; s = substr(s, 1, length(s) - 3) } print s o }'
}

# Prints the CONF_FILE list for a set, or fails on an unknown name.
conf_list() {
	list=prj.conf
	for name in $(echo "$1" | tr '+' ' '); do
		[ "$name" = stock ] && name="bt-host openthread nrf-security"
		for n in $name; do
			if [ -f "$app/stacks/$n.conf" ]; then
				list="$list;stacks/$n.conf"
			elif [ -f "$app/$n.conf" ]; then
				list="$list;$n.conf"
			else
				echo "unknown fragment: $n" >&2
				return 1
			fi
		done
	done
	echo "$list"
}

rc=0
rows=
for set in "$@"; do
	conf=$(conf_list "$set") || { rc=1; rows="$rows| $set | unknown fragment | |
"; continue; }
	dir="$root/$set"
	log="$root/$set.log"
	echo "building $set: $conf" >&2
	if west build -p always -b "$board" -d "$dir" "$app" -- \
		-DZEPHYR_EXTRA_MODULES="$TINYLOCK_MODULES" \
		-DCONF_FILE="$conf" >"$log" 2>&1; then
		flash=$(sed -n 's/^ *FLASH: *\([0-9]*\) B.*/\1/p' "$log" | tail -1)
		ram=$(sed -n 's/^ *RAM: *\([0-9]*\) B.*/\1/p' "$log" | tail -1)
		rows="$rows| $set | $(commas "$flash") | $(commas "$ram") |
"
	else
		rc=1
		rows="$rows| $set | build failed, see $set.log | |
"
	fi
done

echo "| Set | Flash (B) | RAM (B) |"
echo "|---|---|---|"
printf '%s' "$rows"
exit $rc
