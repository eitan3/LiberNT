#!/bin/sh
# Replay $LogFile streams Windows 11 wrote itself and compare the result with the listing
# Windows' own replay of the same capture gave: every file's path, size and SHA-256 must
# match, and no file may be missing or extra.  The images are kept outside the tree (they
# carry metadata written by Windows): NTFSLIB_NTFS_FIXTURES names their directory, which
# holds the images and a truth/ folder.  Skipped when the variable is unset.
set -eu

driver=$1
fixtures=${NTFSLIB_NTFS_FIXTURES:-}

if [ -z "$fixtures" ]; then
    echo "windows log replay: NTFSLIB_NTFS_FIXTURES is not set, skipped"
    exit 77
fi

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-logreplay.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

if [ -f "$fixtures/SHA256SUMS" ]; then
    (cd "$fixtures" && sha256sum --check --quiet SHA256SUMS) || {
        echo "windows log replay: the fixtures fail their SHA256SUMS"
        exit 1
    }
fi

python3 - "$driver" "$fixtures" "$workdir" <<'PY'
import hashlib
import os
import shutil
import struct
import subprocess
import sys

driver, fixtures, workdir = sys.argv[1:4]
truth_dir = os.path.join(fixtures, "truth")

# image, the listing Windows' own replay of the capture gave, whether the captured log is dirty.
# The one-one images are byte patches of real captures made outside the tree (the restart pages'
# version or clean flag changed), modelling a 1.1 log that was not closed cleanly: one with
# nothing to redo, one with the records of dirty-d1 behind a 1.1 page.
cases = [
    ("dirty-ntfs-256m.img", "windows-format-live-after-flush.txt", True),
    ("libernt-format-dirty-d1.img", "libernt-d1-windows-replay.txt", True),
    ("libernt-format-dirty-d2.img", "libernt-d2-windows-replay.txt", True),
    ("libernt-format-clean.img", "libernt-format-live-after-flush.txt", False),
    ("midwrite-b0.img", "midwrite-b0-windows-replay.txt", True),
    ("midwrite-b1.img", "midwrite-b1-windows-replay.txt", True),
    ("one-one-norecords.img", "libernt-format-live-after-flush.txt", True),
    ("one-one-records.img", "libernt-d1-windows-replay.txt", True),
]

failed = False


def fail(message):
    global failed
    failed = True
    print("windows log replay: " + message)


def run(*args):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        sys.exit("windows log replay: %s %s exited with %d: %s" % (
            args[1], args[2], result.returncode,
            result.stderr.decode("utf-8", "replace").strip()))
    return result.stdout


def copy_padded(source, target):
    # A raw volume read stops short of the partition's end: the copy gets the size its
    # boot sector declares, zero-filled.
    with open(source, "rb") as image:
        boot = image.read(512)
    if boot[3:11] != b"NTFS    ":
        sys.exit("windows log replay: %s has no NTFS boot sector" % source)
    bytes_per_sector, = struct.unpack_from("<H", boot, 0x0b)
    sectors, = struct.unpack_from("<Q", boot, 0x28)
    shutil.copyfile(source, target)
    with open(target, "r+b") as image:
        image.truncate(sectors * bytes_per_sector)


def recover(image):
    state = run(driver, "--recover", image).decode("ascii").strip()
    if state != "writable":
        fail("%s: the replay left the volume %s" % (image, state))
        return False
    return True


def listing(image):
    # Every file reachable from the root, as Windows wrote it: X:\<path>, size, SHA-256.
    files = {}
    pending = ["/"]
    while pending:
        path = pending.pop()
        for line in run(driver, "--list", image, path).decode(
                "utf-8", "surrogateescape").splitlines():
            kind, size, name = line.split(None, 2)
            child = path + name if path.endswith("/") else path + "/" + name
            if kind == "d":
                pending.append(child)
                continue
            if kind != "-":
                fail("%s: %s is neither a file nor a directory" % (image, child))
                continue
            digest = hashlib.sha256(run(driver, "--cat", image, child)).hexdigest().upper()
            files[child[1:].replace("/", "\\")] = (int(size), digest)
    return files


def truth(name):
    files = {}
    with open(os.path.join(truth_dir, name), encoding="utf-8") as lines:
        for line in lines:
            if not line.strip():
                continue
            path, size, digest = line.rstrip("\r\n").rsplit(" ", 2)
            # The first truths carry a drive letter (Y:\dir\file), the later ones start at the root (\dir\file)
            path = path[3:] if path[1:2] == ":" else path.lstrip("\\")
            files[path] = (int(size), digest.upper())
    return files


def compare(image, ours, theirs, truth_name):
    problems = 0
    for name in sorted(theirs):
        if name not in ours:
            fail("%s: missing after the replay: %s" % (image, name))
            problems += 1
            continue
        if ours[name][0] != theirs[name][0]:
            fail("%s: %s: size %d, Windows' replay has %d" % (
                image, name, ours[name][0], theirs[name][0]))
            problems += 1
        if theirs[name][1] and ours[name][1] != theirs[name][1]:
            fail("%s: %s: SHA-256 %s, Windows' replay has %s" % (
                image, name, ours[name][1], theirs[name][1]))
            problems += 1
    for name in sorted(set(ours) - set(theirs)):
        fail("%s: extra after the replay: %s" % (image, name))
        problems += 1
    unhashed = sorted(name for name in theirs if not theirs[name][1])
    print("%s: %d files against %s, %d difference(s)%s" % (
        image, len(ours), truth_name, problems,
        "; no hash in the truth for " + ", ".join(unhashed) if unhashed else ""))
    return problems == 0


for image_name, truth_name, dirty in cases:
    source = os.path.join(fixtures, image_name)
    image = os.path.join(workdir, image_name)
    copy_padded(source, image)
    theirs = truth(truth_name)
    if dirty:
        if not recover(image):
            continue
    if not compare(image, listing(image), theirs, truth_name):
        continue
    if not dirty:
        # A clean log opens for writing too, and that open must change no file.
        if not recover(image):
            continue
        compare(image, listing(image), theirs, truth_name)
    else:
        # The replayed log is empty: a second open replays nothing and changes no file.
        if not recover(image):
            continue
        compare(image, listing(image), theirs, truth_name)

sys.exit(1 if failed else 0)
PY
