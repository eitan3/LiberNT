#!/bin/sh
# A volume is opened for writing only when its log leaves the metadata in a known state: an LFS
# 1.1 log that was not closed cleanly is replayed when the reader knows its record layout (the one
# the 2.0 pages carry), stays read-only for any other layout, and a clean 1.1 log is written to
# without a replay.  A hibernated system keeps its volume read-only.
set -eu

driver=$1
mkntfs=$2
ntfsinfo=$3
ntfscp=$4

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-dirtylog.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

state() {
    "$driver" --recover "$1" 2>/dev/null || true
}

make_image() {
    truncate -s 32M "$1"
    "$mkntfs" -F -Q -q "$1" >/dev/null 2>&1
}

# Write two LFS 1.1 restart pages at the start of $LogFile: $2 = restart area flags
# (2 = closed cleanly), $3 = RecordHeaderLength, $4 = LogPageDataOffset.  Client 0 is in use,
# the log's size is the allocation size of $LogFile (the reader refuses a larger one).
write_log() {
    offset=$("$ntfsinfo" -f -v -i 2 "$1" 2>/dev/null | awk '/Runlist:/ { getline; print $2; exit }')
    cluster=$("$ntfsinfo" -f -m "$1" 2>/dev/null | awk '/Cluster Size:/ { print $3; exit }')
    python3 - "$1" "$offset" "$cluster" "$2" "$3" "$4" <<'PY'
import struct, sys
image, lcn, cluster, flags, record_header, page_data = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5], 16), int(sys.argv[6], 16)

def logfile_size():
    # The allocation size of $LogFile (MFT record 2): the lengths of its $DATA runlist, in bytes
    with open(image, 'rb') as f:
        boot = f.read(512)
        cluster_size = struct.unpack_from('<H', boot, 0x0b)[0] * boot[0x0d]
        mft_lcn, = struct.unpack_from('<Q', boot, 0x30)
        per_record = struct.unpack_from('<b', boot, 0x40)[0]
        record_size = per_record * cluster_size if per_record > 0 else 1 << -per_record
        f.seek(mft_lcn * cluster_size + 2 * record_size)
        record = f.read(record_size)
    offset, = struct.unpack_from('<H', record, 0x14)
    while True:
        attr_type, attr_length = struct.unpack_from('<II', record, offset)
        if attr_type == 0x80 and record[offset + 8]:
            break
        offset += attr_length
    pos, clusters = offset + struct.unpack_from('<H', record, offset + 0x20)[0], 0
    while record[pos]:
        header, pos = record[pos], pos + 1
        length_size, offset_size = header & 0xf, header >> 4
        clusters += int.from_bytes(record[pos:pos + length_size], 'little')
        pos += length_size + offset_size
    return clusters * cluster_size

page = 4096
def restart_page():
    p = bytearray(page)
    p[0:4] = b'RSTR'
    struct.pack_into('<HH', p, 4, 0x1e, page // 512 + 1)
    struct.pack_into('<IIHHH', p, 0x10, page, page, 0x30, 1, 1)
    area = 0x30
    struct.pack_into('<QHHHHI', p, area, 0x1000, 1, 0xffff, 0, flags, 44)
    struct.pack_into('<HHQ', p, area + 0x14, 0x30, 0x30, logfile_size())
    struct.pack_into('<HH', p, area + 0x24, record_header, page_data)
    usn = 1
    struct.pack_into('<H', p, 0x1e, usn)
    for i in range(page // 512):
        end = (i + 1) * 512 - 2
        p[0x20 + 2 * i:0x22 + 2 * i] = p[end:end + 2]
        struct.pack_into('<H', p, end, usn)
    return p
with open(image, 'r+b') as f:
    f.seek(lcn * cluster)
    f.write(restart_page() + restart_page())
PY
}

# The first four bytes of $LogFile after write_log: RSTR while the restart pages stand, ffffffff
# once a replay has reset the log (the pages carry no client restart record, so there is nothing to redo).
log_head() {
    python3 -c 'import sys; f = open(sys.argv[1], "rb"); f.seek(int(sys.argv[2], 16) * int(sys.argv[3])); print(f.read(4).hex())' "$1" "$offset" "$cluster"
}

image="$workdir/clean.ntfs"
make_image "$image"
result=$(state "$image")
[ "$result" = writable ] || { echo "blank log: $result, expected writable"; exit 1; }

write_log "$image" 2 0x30 0x40
result=$(state "$image")
[ "$result" = writable ] || { echo "clean 1.1 log: $result, expected writable"; exit 1; }
[ "$(log_head "$image")" = 52535452 ] || { echo "clean 1.1 log: the open reset \$LogFile, expected no replay"; exit 1; }

image="$workdir/unclean-known.ntfs"
make_image "$image"
write_log "$image" 0 0x30 0x40
result=$(state "$image")
[ "$result" = writable ] || { echo "unclean 1.1 log with the known layout: $result, expected writable (replayed)"; exit 1; }
[ "$(log_head "$image")" = ffffffff ] || { echo "unclean 1.1 log with the known layout: \$LogFile was not replayed"; exit 1; }

image="$workdir/unclean-unknown.ntfs"
make_image "$image"
# Page data at 0x38 is a layout the reader would not refuse by itself: only the rule keeps it out.
write_log "$image" 0 0x30 0x38
result=$(state "$image")
[ "$result" = read-only ] || { echo "unclean 1.1 log with an unknown layout: $result, expected read-only"; exit 1; }
[ "$(log_head "$image")" = 52535452 ] || { echo "unclean 1.1 log with an unknown layout: the read-only open changed \$LogFile"; exit 1; }

image="$workdir/hibernated.ntfs"
make_image "$image"
printf 'hibr' > "$workdir/hiberfil.sys"
"$ntfscp" -f -q "$image" "$workdir/hiberfil.sys" /hiberfil.sys
result=$(state "$image")
[ "$result" = read-only ] || { echo "hibernated: $result, expected read-only"; exit 1; }

echo "dirty log read-only smoke passed"
