#!/usr/bin/env python3
"""Generate the Chicago-95 boot module directory and module payloads.

The directory is a packed 2-sector image at LBA 0x5000.  Payload images use
the module_image_header_t ABI from include/boot/bootinfo.h and are placed at
LBA 0x5100 + index * 16 by tools/make_image.cmake.
"""

import argparse
import os
import struct

MODULE_DIR_MAGIC = 0x4C444F4D
MODULE_DIR_LBA = 0x5000
MODULE_DIR_SECTORS = 2
MODULE_IMAGE_MAGIC = 0x31444F4D  # "MOD1"
MODULE_IMAGE_VERSION = 1
MODULE_FLAG_DATA = 0x02
MODULE_NAME_LEN = 32
MODULE_MAX = 16
MODULE_LBA = 0x5100
MODULE_SECTORS = 1
MODULE_LOAD_ADDR = 0x700000
SECTOR_SIZE = 512
MODULE_STRIDE_SECTORS = 16

MODULES = [
    (
        "hello",
        "Chicago-95 boot module 'hello' is online.\n",
    ),
    (
        "netprobe",
        "Chicago-95 netprobe: link layer scan placeholder ready.\n",
    ),
    (
        "storageprobe",
        "Chicago-95 storageprobe: ATA/NVME enumeration placeholder ready.\n",
    ),
]


if struct.calcsize("<II32sQII") != 56:
    raise RuntimeError("module_image_header_t ABI does not match bootinfo.h")
if struct.calcsize("<32sIII") != 44:
    raise RuntimeError("module directory entry ABI does not match stage3/disk.h")


def fixed_name(name: str) -> bytes:
    raw = name.encode("ascii")[: MODULE_NAME_LEN - 1]
    return raw + b"\0" * (MODULE_NAME_LEN - len(raw))


def make_payload(index: int, name: str, text: str) -> bytes:
    header = struct.pack(
        "<II32sQII",
        MODULE_IMAGE_MAGIC,
        MODULE_IMAGE_VERSION,
        fixed_name(name),
        0,
        MODULE_FLAG_DATA,
        0,
    )
    body = text.encode("ascii")
    payload = header + body
    if len(payload) > MODULE_SECTORS * SECTOR_SIZE:
        raise ValueError(f"module {name} is too large")
    return payload.ljust(MODULE_SECTORS * SECTOR_SIZE, b"\0")


def make_directory() -> bytes:
    if len(MODULES) > MODULE_MAX:
        raise ValueError(f"too many modules: {len(MODULES)} > {MODULE_MAX}")

    data = struct.pack("<II8x", MODULE_DIR_MAGIC, len(MODULES))
    for index, (name, _text) in enumerate(MODULES):
        data += struct.pack(
            "<32sIII",
            fixed_name(name),
            MODULE_LBA + index * MODULE_STRIDE_SECTORS,
            MODULE_SECTORS,
            MODULE_LOAD_ADDR + index * MODULE_STRIDE_SECTORS * SECTOR_SIZE,
        )
    return data.ljust(MODULE_DIR_SECTORS * SECTOR_SIZE, b"\0")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    with open(os.path.join(args.output_dir, "module_dir.bin"), "wb") as out:
        out.write(make_directory())

    for index, (name, text) in enumerate(MODULES):
        with open(os.path.join(args.output_dir, f"module_{index}.bin"), "wb") as out:
            out.write(make_payload(index, name, text))

    print(
        f"generated {len(MODULES)} boot modules "
        f"(dir LBA 0x{MODULE_DIR_LBA:x}, first payload LBA 0x{MODULE_LBA:x})"
    )


if __name__ == "__main__":
    main()
