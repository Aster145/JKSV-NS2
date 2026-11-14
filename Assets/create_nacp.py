#!/usr/bin/env python3
"""Keep NACP titles UTF-8 even when Windows nacptool uses ANSI argv."""

from pathlib import Path
import subprocess
import sys


def text_field(value: str, size: int) -> bytes:
    encoded = value.encode('utf-8')
    if b'\0' in encoded or len(encoded) >= size:
        raise ValueError(f'NACP text must fit in {size - 1} UTF-8 bytes without NULs')
    return encoded.ljust(size, b'\0')


def main() -> None:
    title, author, version, output, *flags = sys.argv[1:]
    # NacpLanguageEntry: name[0x200], author[0x100], repeated 16 times.
    entry = text_field(title, 0x200) + text_field(author, 0x100)
    # Let the SDK tool generate all remaining fields, including title-id options.
    subprocess.run(['nacptool', '--create', 'JKSV', 'JK', version, output, *flags], check=True)
    path = Path(output)
    nacp = bytearray(path.read_bytes())
    if len(nacp) != 0x4000:
        raise ValueError('Unexpected nacptool output size')
    nacp[:0x3000] = entry * 16
    path.write_bytes(nacp)


if __name__ == '__main__':
    main()
