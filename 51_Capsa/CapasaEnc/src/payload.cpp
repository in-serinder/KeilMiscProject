// payload.cpp - trailer/header parsing, decryption and archive extraction.
#include "payload.h"

#include "aes.h"
#include "io.h"
#include "lz4.h"
#include "sha256.h"

static bool fail(char *err, size_t cap, const char *msg)
{
    if (err && cap) xstrncpy(err, msg, cap);
    return false;
}

bool payload_probe(const char *exePath, PayloadInfo *info, char *err, size_t errCap)
{
    u8 *file = nullptr;
    u64 size = 0;

    if (!file_read_all(exePath, &file, &size))
    {
        return fail(err, errCap, "cannot read the executable");
    }

    bool ok = false;
    if (size < kFooterLen + kHeaderFixed)
    {
        fail(err, errCap, "file is too small to hold a payload");
        goto done;
    }

    {
        const u8 *footer = file + size - kFooterLen;
        if (xmemcmp(footer + kFooterMagicField, kFooterMagic, 8) != 0)
        {
            fail(err, errCap, "no embedded payload found");
            goto done;
        }

        const u64 offset = rd64(footer + kFooterOffsetField);
        const u64 payloadSize = rd64(footer + kFooterSizeField);

        if (offset < 1 || offset > size || payloadSize > size || offset + payloadSize + kFooterLen > size)
        {
            fail(err, errCap, "payload offsets are corrupt");
            goto done;
        }

        const u8 *header = file + offset;
        if (xmemcmp(header + kMagicOffset, kMagic, 8) != 0)
        {
            fail(err, errCap, "payload header magic mismatch");
            goto done;
        }

        info->flags = header[kFlagsOffset];
        info->iterations = rd32(header + kIterOffset);
        xmemcpy(info->salt, header + kSaltOffset, kSaltLen);
        xmemcpy(info->nonce, header + kNonceOffset, kNonceLen);
        info->plainLen = rd64(header + kPlainLenOffset);
        info->dataLen = rd64(header + kDataLenOffset);

        u32 nameLen = rd16(header + kNameLenOffset);
        if (nameLen > kMaxNameBytes)
        {
            fail(err, errCap, "payload name is too long");
            goto done;
        }

        xmemset(info->name, 0, sizeof(info->name));
        if (offset + kHeaderFixed + nameLen > size)
        {
            fail(err, errCap, "payload name runs past the file");
            goto done;
        }
        xmemcpy(info->name, header + kHeaderFixed, nameLen);
        info->payloadOffset = offset;
        ok = true;
    }

done:
    xfree(file);
    return ok;
}

bool payload_load(const char *exePath, const char *password, u8 **plain, u64 *plainSize,
                  PayloadInfo *info, char *err, size_t errCap)
{
    *plain = nullptr;
    *plainSize = 0;

    if (!payload_probe(exePath, info, err, errCap)) return false;

    u8 *file = nullptr;
    u64 size = 0;
    if (!file_read_all(exePath, &file, &size))
    {
        return fail(err, errCap, "cannot read the executable");
    }

    const u64 cipherOffset = info->payloadOffset + kHeaderFixed + xstrlen(info->name);
    if (cipherOffset + info->dataLen + kTagLen > size)
    {
        xfree(file);
        return fail(err, errCap, "payload is truncated");
    }

    u8 key[kKeyLen];
    pbkdf2_sha256(password, xstrlen(password), info->salt, kSaltLen,
                  info->iterations ? info->iterations : kDefaultIterations, key);

    u8 *buffer = (u8 *)xalloc((size_t)(info->dataLen ? info->dataLen : 1));
    if (!buffer)
    {
        xfree(file);
        return fail(err, errCap, "out of memory");
    }

    const bool tagOk = gcm_decrypt(key, info->nonce, file + cipherOffset, (size_t)info->dataLen,
                                   buffer, file + cipherOffset + info->dataLen);
    xfree(file);

    if (!tagOk)
    {
        xfree(buffer);
        return fail(err, errCap, "wrong password or the payload was modified");
    }

    if (info->flags & kFlagCompressed)
    {
        u8 *expanded = (u8 *)xalloc((size_t)(info->plainLen ? info->plainLen : 1));
        if (!expanded)
        {
            xfree(buffer);
            return fail(err, errCap, "out of memory");
        }

        const size_t n = lz4_decompress_block(buffer, (size_t)info->dataLen, expanded, (size_t)info->plainLen);
        xfree(buffer);

        if (n != (size_t)info->plainLen)
        {
            xfree(expanded);
            return fail(err, errCap, "decompression failed");
        }

        *plain = expanded;
        *plainSize = info->plainLen;
    }
    else
    {
        *plain = buffer;
        *plainSize = info->dataLen;
    }

    return true;
}

// ---- archive extraction ------------------------------------------------- //

static void show_progress(u64 done, u64 total)
{
    if (!stdout_is_tty()) return;

    char line[80];
    size_t p = 0;
    p = fmt_str(line, p, "  [");

    const u32 width = 28;
    u32 filled = total ? (u32)(done * width / total) : width;
    if (filled > width) filled = width;
    for (u32 i = 0; i < width; ++i) line[p++] = (i < filled) ? '#' : '-';

    p = fmt_str(line, p, "] ");
    u32 percent = total ? (u32)(done * 100 / total) : 100;
    if (percent > 100) percent = 100;
    if (percent < 10) line[p++] = ' ';
    if (percent < 100) line[p++] = ' ';
    p = fmt_u64(line, p, percent);
    p = fmt_str(line, p, "%  ");
    p = fmt_size(line, p, done);
    p = fmt_str(line, p, " / ");
    p = fmt_size(line, p, total);
    line[p++] = '\r';

    out_raw(line, p);
}

bool archive_extract(const u8 *plain, u64 size, const char *outDir, bool assumeYes,
                     char *err, size_t errCap)
{
    u64 pos = 0;
    if (size < 4) return fail(err, errCap, "archive is empty");

    const u32 count = rd32(plain + pos);
    pos += 4;

    if (count == 0 || count > 1000000) return fail(err, errCap, "archive entry count is invalid");

    const u64 totalBytes = size; // progress is reported against the archive size

    u64 done = 0;

    for (u32 i = 0; i < count; ++i)
    {
        if (pos + 1 + 2 > size) return fail(err, errCap, "archive entry header is truncated");

        const u8 type = plain[pos++];
        const u16 nameLen = rd16(plain + pos);
        pos += 2;

        if (nameLen == 0 || nameLen > kMaxNameBytes * 2 || pos + nameLen > size)
        {
            return fail(err, errCap, "archive entry name is invalid");
        }

        char rel[kMaxNameBytes * 2 + 1];
        xmemset(rel, 0, sizeof(rel));
        xmemcpy(rel, plain + pos, nameLen);
        pos += nameLen;

        // Reject absolute paths and any ".." component (zip-slip protection).
        if (rel[0] == '/' || rel[0] == '\\' || (rel[0] && rel[1] == ':'))
        {
            return fail(err, errCap, "archive contains an absolute path");
        }
        for (u32 k = 0; rel[k]; ++k)
        {
            if (rel[k] == '.' && rel[k + 1] == '.')
            {
                return fail(err, errCap, "archive contains a parent-directory path");
            }
        }

        char target[1024];
        path_join(target, sizeof(target), outDir, rel);

        if (type == kEntryDir)
        {
            dir_create(target);
            continue;
        }

        u64 fileSize = 0;
        u64 mtime = 0;
        if (pos + 16 > size) return fail(err, errCap, "archive entry body is truncated");
        fileSize = rd64(plain + pos);
        mtime = rd64(plain + pos + 8);
        pos += 16;

        if (pos + fileSize > size) return fail(err, errCap, "archive entry data is truncated");

        if (file_exists(target) && !assumeYes)
        {
            char prompt[512];
            size_t p = 0;
            p = fmt_str(prompt, p, "  exists: ");
            p = fmt_str(prompt, p, rel);
            p = fmt_str(prompt, p, "  [y]overwrite / [n]skip / [c]ancel > ");
            prompt[p] = 0;
            out_str(prompt);

            char answer[8];
            read_line(answer, sizeof(answer));

            if (answer[0] == 'c' || answer[0] == 'C')
            {
                return fail(err, errCap, "cancelled by user");
            }
            if (!(answer[0] == 'y' || answer[0] == 'Y'))
            {
                pos += fileSize;
                done += fileSize;
                continue;
            }
        }

        dir_create_for_file(target);
        if (!file_write_all(target, plain + pos, fileSize))
        {
            return fail(err, errCap, "cannot write an extracted file");
        }
        if (mtime) file_set_mtime(target, mtime);   // best effort

        pos += fileSize;
        done += fileSize;
        show_progress(done, totalBytes);
    }

    if (stdout_is_tty()) out_str("\n");
    return true;
}
