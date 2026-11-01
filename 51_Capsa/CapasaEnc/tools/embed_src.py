#!/usr/bin/env python3
"""embed_src.py - pack the capsaenc sources into a C array for the --src option.

Builds the same archive layout the packer uses for folders (entry count,
[name][type/sizes] entries), LZ4-compresses it and emits src/src_blob.h with
`g_src_blob`. No encryption: the source is meant to be freely extractable.

Usage: python tools/embed_src.py <src_dir> <out_header>
"""

import os
import struct
import sys

# Minimal pure-python LZ4 block compressor (same format as src/lz4.cpp).
# Good enough for a build tool; input is small and compresses well.


def lz4_compress(data: bytes) -> bytes:
    n = len(data)
    if n >= 1 << 31:
        raise ValueError("too large")

    # token-based greedy matcher with a 4-byte hash table
    hash_table = {}
    out = bytearray()
    # Note: pure LZ4 block format, no size header - matches src/lz4.cpp.

    def emit_literal(src, lit_len):
        token = (lit_len if lit_len < 15 else 15) << 4
        out.append(token)
        if lit_len >= 15:
            rem = lit_len - 15
            while rem >= 255:
                out.append(255)
                rem -= 255
            out.append(rem)
        out.extend(data[src:src + lit_len])

    def emit_match(match_pos, lit_len, match_len):
        # match_len excludes the 4 minimum bytes
        token = (lit_len if lit_len < 15 else 15) << 4
        ml = match_len - 4
        if ml >= 15:
            token |= 15
        else:
            token |= ml
        out.append(token)
        if lit_len >= 15:
            rem = lit_len - 15
            while rem >= 255:
                out.append(255)
                rem -= 255
            out.append(rem)
        out.extend(data[lit_start:src_pos])   # literal bytes
        off = src_pos - match_pos
        out.extend(struct.pack('<H', off))
        if ml >= 15:
            rem = ml - 15
            while rem >= 255:
                out.append(255)
                rem -= 255
            out.append(rem)

    src_pos = 0
    lit_start = 0
    while src_pos < n:
        best_len = 0
        best_pos = 0
        if src_pos + 4 <= n:
            key = data[src_pos:src_pos + 4]
            cand = hash_table.get(key)
            hash_table[key] = src_pos
            if cand is not None and src_pos - cand <= 0xFFFF:
                # extend the match
                l = 0
                while (src_pos + l < n and cand + l < src_pos
                       and data[cand + l] == data[src_pos + l]):
                    l += 1
                if l >= 4:
                    best_len = l
                    best_pos = cand
        if best_len >= 4:
            lit_len = src_pos - lit_start
            emit_match(best_pos, lit_len, best_len)
            src_pos += best_len
            lit_start = src_pos
            # index the skipped bytes so later matches can find them
            for i in range(src_pos - best_len + 1, min(src_pos, max(0, n - 3))):
                k = data[i:i + 4]
                if k not in hash_table:
                    hash_table[k] = i
        else:
            src_pos += 1
    if lit_start < n:
        emit_literal(lit_start, n - lit_start)
    return bytes(out)


def lz4_decompress_verify(packed: bytes, expect_len: int) -> bytes:
    """Pure-python LZ4 block decompressor used to verify what we emit."""
    pos = 0
    out = bytearray()
    while pos < len(packed):
        token = packed[pos]; pos += 1
        lit = token >> 4            # high nibble = literal length
        if lit == 15:
            while True:
                b = packed[pos]; pos += 1
                lit += b
                if b != 255:
                    break
        out.extend(packed[pos:pos + lit]); pos += lit
        if pos >= len(packed):
            break
        off = packed[pos] | (packed[pos + 1] << 8); pos += 2
        ml = token & 0x0F           # low nibble = match length - 4
        if ml == 15:
            while True:
                b = packed[pos]; pos += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        start = len(out) - off
        if start < 0:
            raise ValueError("bad offset")
        for i in range(ml):
            out.append(out[start + i])
    if len(out) != expect_len:
        raise ValueError(f"length mismatch {len(out)} != {expect_len}")
    return bytes(out)


def main():
    src_dir, out_path = sys.argv[1], sys.argv[2]

    entries = []  # (rel, is_dir, mtime_ms, data)
    for root, dirs, files in os.walk(src_dir):
        dirs.sort()
        rel_root = os.path.relpath(root, src_dir).replace('\\', '/')
        if rel_root != '.':
            entries.append((rel_root + '/', True, 0, b''))
        for fn in sorted(files):
            if fn == 'stub_blob.h' or fn == 'src_blob.h':
                continue  # generated files
            full = os.path.join(root, fn)
            rel = (rel_root + '/' if rel_root != '.' else '') + fn
            with open(full, 'rb') as f:
                entries.append((rel, False, 0, f.read()))

    # sanity: verify our compressor round-trips against itself
    blob = bytearray(struct.pack('<I', len(entries)))
    for rel, is_dir, mtime, data in entries:
        rb = rel.encode('utf-8')
        blob += struct.pack('<BH', 1 if is_dir else 0, len(rb)) + rb
        if not is_dir:
            blob += struct.pack('<QQ', len(data), mtime) + data

    packed = lz4_compress(bytes(blob))
    # Full round-trip verification with an independent decompressor: the
    # C-side lz4_decompress_block must be able to consume this output.
    back = lz4_decompress_verify(packed, len(blob))
    assert back == blob, "lz4 round-trip mismatch in embed_src.py"

    name = os.path.basename(out_path).replace('.', '_')
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write("// generated by tools/embed_src.py - do not edit\n")
        f.write("#pragma once\n#include \"common.h\"\n\n")
        f.write("static const unsigned char g_src_blob[] = {\n")
        for i in range(0, len(packed), 16):
            chunk = packed[i:i + 16]
            f.write("    " + ",".join(str(b) for b in chunk) + ",\n")
        f.write("};\n")
        f.write("static const u64 g_src_blob_len = sizeof(g_src_blob);\n")
        f.write(f"static const u64 g_src_blob_raw_len = {len(blob)}; // uncompressed size\n")

    print(f"embedded {len(entries)} entries "
          f"({len(blob)} B raw -> {len(packed)} B lz4) -> {out_path}")


if __name__ == '__main__':
    main()
