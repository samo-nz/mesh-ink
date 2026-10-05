"""Build and verify a complete 16 MiB ESP32-S3 MeshInk flash image.

The image intentionally leaves all data partitions erased (0xFF). With MeshInk's
OTA-only app table and no factory partition, erased OTA data makes ESP-IDF boot
ota_0 on first boot.
"""

from pathlib import Path
import argparse
import hashlib

FLASH_SIZE = 16 * 1024 * 1024
BOOTLOADER_OFFSET = 0x0000
PARTITIONS_OFFSET = 0x8000
APP_OFFSET = 0x10000
APP0_SIZE = 0x600000

def place(image: bytearray, source: Path, offset: int, limit: int | None = None) -> bytes:
    data = source.read_bytes()
    if not data:
        raise SystemExit(f"empty input: {source}")
    if limit is not None and len(data) > limit:
        raise SystemExit(f"{source} is too large: {len(data)} > {limit}")
    end = offset + len(data)
    if end > len(image):
        raise SystemExit(f"{source} exceeds flash image at 0x{offset:x}")
    image[offset:end] = data
    return data

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bootloader", required=True, type=Path)
    parser.add_argument("--partitions", required=True, type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    image = bytearray(b"\xff") * FLASH_SIZE
    bootloader = place(image, args.bootloader, BOOTLOADER_OFFSET, PARTITIONS_OFFSET)
    partitions = place(image, args.partitions, PARTITIONS_OFFSET, 0x1000)
    firmware = place(image, args.firmware, APP_OFFSET, APP0_SIZE)

    # Partition-table/data gap (0x9000..0xffff) must remain erased. This covers
    # NVS and OTA data in partitions.csv.
    if any(byte != 0xFF for byte in image[0x9000:APP_OFFSET]):
        raise SystemExit("data-partition gap is not erased")

    args.output.write_bytes(image)
    output = args.output.read_bytes()
    if len(output) != FLASH_SIZE:
        raise SystemExit(f"full-wipe size mismatch: {len(output)}")
    if output[BOOTLOADER_OFFSET:BOOTLOADER_OFFSET+len(bootloader)] != bootloader:
        raise SystemExit("bootloader verification failed")
    if output[PARTITIONS_OFFSET:PARTITIONS_OFFSET+len(partitions)] != partitions:
        raise SystemExit("partition table verification failed")
    if output[APP_OFFSET:APP_OFFSET+len(firmware)] != firmware:
        raise SystemExit("firmware verification failed")
    if any(byte != 0xFF for byte in output[0x9000:APP_OFFSET]):
        raise SystemExit("erased data-partition verification failed")

    digest = hashlib.sha256(output).hexdigest()
    print(f"PASS: {args.output} {len(output)} bytes sha256={digest}")

if __name__ == "__main__":
    main()
