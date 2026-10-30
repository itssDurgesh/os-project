"""Writes a 5 MB test file. That's big enough to need double-indirect blocks, and
the kernel can recompute each byte to check it read the file correctly."""

import sys

SIZE = 5 * 1024 * 1024

with open(sys.argv[1], "wb") as output:
    output.write(bytes((i ^ (i >> 8) ^ (i >> 16)) & 0xFF for i in range(SIZE)))
