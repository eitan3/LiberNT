#!/bin/bash
#
# PROJECT:     LiberNT Intel Smart Sound DSP driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Builds sof-ipc4-loopback.tplg from sof-ipc4-loopback.conf with Sound Open Firmware's topology2 classes
# COPYRIGHT:   Copyright 2026 LiberNT contributors
#
# NOTICE: sof-ipc4-loopback.tplg was built by the LiberNT project from
# sof-ipc4-loopback.conf and the topology2 classes of Sound Open Firmware
# (https://github.com/thesofproject/sof), BSD-3-Clause, Copyright Intel.
# It embeds no NHLT table and no third-party binary. It is shipped under
# LICENCE.Intel, the licence the firmware release ships with, for uniformity.
#
# usage: build.sh <path to a Sound Open Firmware checkout> [output file]
#
# The checkout must stand at the revision fwload.h names, where the classes
# this topology instantiates have the layouts the plan decoded. The build is
# the one SOF's own CMake performs: the ABI block their get_abi.sh emits is
# prepended to the conf, and alsatplg's pre-processor (-p) resolves the
# classes against the topology2 directory before the topology is compiled.
# alsatplg 1.2.7 or newer is required; the revision built here was checked
# with alsatplg 1.2.16 (alsa-utils 1.2.16) and libatopology 1.2.16.1.
#
set -eu

SOF_REVISION=fcebbbf22d7c08159fbb8486d5f3687da4f167c3

if [ $# -lt 1 ]; then
	echo "usage: $0 <sof checkout> [output file]" >&2
	exit 2
fi
SOF=$1
HERE=$(cd "$(dirname "$0")" && pwd)
OUTPUT=${2:-$HERE/../sof-ipc4-loopback.tplg}
TOPOLOGY2=$SOF/tools/topology/topology2

if [ "$(git -C "$SOF" rev-parse HEAD)" != "$SOF_REVISION" ]; then
	echo "$SOF is not at $SOF_REVISION" >&2
	exit 1
fi
if ! command -v alsatplg >/dev/null; then
	echo "alsatplg is not installed" >&2
	exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
"$TOPOLOGY2/get_abi.sh" "$SOF" ipc4 > "$WORK/abi.conf"
cat "$WORK/abi.conf" "$HERE/sof-ipc4-loopback.conf" > "$WORK/sof-ipc4-loopback.conf"
ALSA_CONFIG_DIR="$TOPOLOGY2" alsatplg -I "$TOPOLOGY2" -p -c "$WORK/sof-ipc4-loopback.conf" -o "$OUTPUT"
sha256sum "$OUTPUT"
