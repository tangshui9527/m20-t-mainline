#!/usr/bin/env python3
"""Wrap a raw DTB into Qualcomm QCDT v3 table format (and optionally rebuild boot.img).

Root cause fixed here (2026-10-30): the M20-T (MSM8909) stock aboot validates the
boot.img DT segment with dev_tree_validate(), which requires the dtbTool "QCDT"
table magic (0x54444351). A bare FDT blob (0xd00dfeed) is rejected with
"ERROR: Bad magic in device tree table" -> boot fails -> aboot falls back to
fastboot (USB 18d1:d00d), kernel never launches.

QCDT v3 layout (reverse-engineered from stock boot.img dt segment + aboot
dev_tree_validate @0x8f612c50):
  header:  magic(4)="QCDT"  version(4)=3  num_entries(4)
  entry :  40 bytes = soc, board, subtype, ?, pmic[4], offset, size  (LE u32)
  payload offsets aligned to 0x800 (stock table convention).

dtb17 entry (the one stock aboot selects on this device, see docs/log.md):
  soc=0xf5 (245/msm8909) board=0x1000b subtype=0x0

Usage:
  wrap-dt-qcdt.py wrap    <dtb> <out.qcdt>
  wrap-dt-qcdt.py rebuild <bootimg> <dtb> <out-bootimg>
"""
import struct
import sys

DTB17_SOC, DTB17_BOARD, DTB17_SUBTYPE = 0xF5, 0x1000B, 0x0
PAGE = 2048


def roundup(x, n):
    return (x + n - 1) // n * n


def make_qcdt(dtb: bytes, soc=DTB17_SOC, board=DTB17_BOARD, subtype=DTB17_SUBTYPE) -> bytes:
    hdr_size = 12 + 40
    off = roundup(hdr_size, 0x800)
    table = struct.pack("<III", 0x54444351, 3, 1)          # QCDT, v3, 1 entry
    table += struct.pack("<10I", soc, board, subtype, 0, 0, 0, 0, 0, off, len(dtb))
    table += b"\x00" * (off - len(table))
    return table + dtb


def rebuild_bootimg(src: bytes, dtb: bytes) -> bytes:
    magic = src[0:8]
    assert magic == b"ANDROID!", magic
    ks, ka, rs, ra, ss, sa, tags, page, dt_old, unused = struct.unpack_from("<10I", src, 8)
    assert page == PAGE, f"unexpected page size {page}"

    qcdt = make_qcdt(dtb)
    dt_size = roundup(len(qcdt), PAGE)                      # stock convention: page-aligned

    kernel = src[PAGE:PAGE + ks]
    off_rd = PAGE * (1 + roundup(ks, PAGE) // PAGE)
    ramdisk = src[off_rd:off_rd + rs]

    hdr = bytearray(src[:PAGE])
    struct.pack_into("<I", hdr, 40, dt_size)                # patch dt_size only

    out = bytes(hdr) + kernel + b"\x00" * (roundup(ks, PAGE) - ks)
    out += ramdisk + b"\x00" * (roundup(rs, PAGE) - rs)
    out += qcdt + b"\x00" * (dt_size - len(qcdt))
    assert len(out) == PAGE + roundup(ks, PAGE) + roundup(rs, PAGE) + dt_size
    return out


def verify(img: bytes):
    ks, ka, rs, ra, ss, sa, tags, page, dt, _ = struct.unpack_from("<10I", img, 8)
    off_dt = PAGE * (1 + roundup(ks, PAGE) // PAGE + roundup(rs, PAGE) // PAGE)
    m, ver, num = struct.unpack_from("<III", img, off_dt)
    assert m == 0x54444351, f"dt segment magic {m:#x} != QCDT"
    assert ver == 3 and num == 1
    e = struct.unpack_from("<10I", img, off_dt + 12)
    off, size = e[8], e[9]
    assert struct.unpack_from(">I", img, off_dt + off)[0] == 0xD00DFEED, "payload not FDT"
    print(f"verify OK: dt@{off_dt:#x} QCDT v3 x{num} soc={e[0]:#x} board={e[1]:#x} "
          f"subtype={e[2]:#x} payload={size}B dt_size={dt}")


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "wrap":
        dtb = open(sys.argv[2], "rb").read()
        qcdt = make_qcdt(dtb)
        open(sys.argv[3], "wb").write(qcdt)
        print(f"wrapped {len(dtb)}B dtb -> {len(qcdt)}B QCDT ({sys.argv[3]})")
    elif len(sys.argv) >= 5 and sys.argv[1] == "rebuild":
        src = open(sys.argv[2], "rb").read()
        dtb = open(sys.argv[3], "rb").read()
        fixed = rebuild_bootimg(src, dtb)
        open(sys.argv[4], "wb").write(fixed)
        verify(fixed)
        print(f"written {sys.argv[4]}: {len(fixed)} bytes (was {len(src)})")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
