#!/bin/sh
set -e
if [ $# -lt 2 ]; then
    echo "usage: mkcard.sh IMAGE HLUX_EXE [--autorun]" >&2
    exit 2
fi
image=$1
executable=$2
autorun=$3
if ! command -v mformat > /dev/null; then
    echo "mkcard.sh: needs mtools (brew install mtools, or apt install mtools)" >&2
    exit 1
fi
mkdir -p "$(dirname "$image")"
python3 -I - "$image" <<'PYTHON'
import struct
import sys

size = 32 * 1024 * 1024
sectors = size // 512
with open(sys.argv[1], "wb") as image:
    image.truncate(size)
    entry = struct.pack("<B3sB3sII", 0, b"\0\x02\0", 0x06, b"\0\0\0", 1, sectors - 1)
    image.seek(446)
    image.write(entry)
    image.seek(510)
    image.write(b"\x55\xaa")
PYTHON
mformat -i "$image@@512" -T 65535 -h 16 -s 32 -v SH3CARD ::
mmd -i "$image@@512" ::/HLUX
mcopy -i "$image@@512" -Q "$executable" ::/HLUX/HLUX.EXE
if [ "$autorun" = --autorun ]; then
    mmd -i "$image@@512" ::/10003
    mcopy -i "$image@@512" -Q "$executable" ::/10003/AUTORUN.EXE
fi
