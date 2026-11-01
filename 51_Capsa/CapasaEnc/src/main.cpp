// main.cpp - the encryptor CLI (freestanding: no CRT, no STL).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "aes.h"
#include "format.h"
#include "io.h"
#include "lz4.h"
#include "payload.h"
#include "sha256.h"
#include "stub_blob.h"
#if __has_include("src_blob.h")
#include "src_blob.h"
#define kHasSrcBlob 1
#else
#define kHasSrcBlob 0
#endif

#define kVersion "1.0.0"

// ---- output helpers ----------------------------------------------------- //

static void say(const char *s) { out_str(s); out_str("\n"); }

static void say2(const char *a, const char *b)
{
    out_str(a);
    out_str(b);
    out_str("\n");
}

// ---- in-place status line (TTY only) ------------------------------------ //

static void status(const char *s)
{
    if (!stdout_is_tty()) return;
    char line[200];
    size_t p = fmt_str(line, 0, "\r  ");
    p = fmt_str(line, p, s);
    while (p + 1 < sizeof(line) && p < 76) line[p++] = ' ';   // erase the tail
    line[p] = 0;
    out_raw(line, xstrlen(line));
}

static void status_end()
{
    if (stdout_is_tty()) out_raw("\r", 1);
}

static void usage()
{
    say("capsaenc " kVersion " - AES-256-GCM self-extracting packer");
    say("");
    say("Usage:");
    say("  capsaenc -f <file>   [-o out.exe] [--max-size 120K] [--no-compress] [--self-delete]");
    say("  capsaenc -d <folder> [-o out.exe] [--max-size 120K] [--no-compress] [--self-delete]");
    say("  capsaenc -t <text>   [-o out.exe] [--max-size 120K] [--no-compress] [--self-delete]");
    say("  capsaenc --info <package.exe>");
    say("  capsaenc --selftest");
    say("");
    say("Options:");
    say("  -o <path>        output executable (default: <name>.exe)");
    say("  --max-size <n>   refuse to produce a package bigger than n (K/M suffix, default 120K)");
    say("  --no-compress    skip LZ4 compression");
    say("  --self-delete    the produced package deletes itself after extraction");
    say("  --info <file>    print the header of an existing package");
    say("  --src [dir]      write capsaenc's own source code into dir (default: ./capsaenc-src)");
    say("  --selftest       run crypto / compression test vectors");
    say("  -h, --help       this help");
    say("");
    say("Password is typed twice when packing and once when extracting (shown as *).");
}

// ---- helpers ------------------------------------------------------------ //

static const char *base_name(const char *path)
{
    const char *last = path;
    for (const char *p = path; *p; ++p)
    {
        if (*p == '\\' || *p == '/') last = p + 1;
    }
    return last;
}

static bool random_bytes(u8 *buf, u32 len)
{
    return BCryptGenRandom(nullptr, buf, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

static void to_hex(char *out, const u8 *data, u32 len)
{
    fmt_hex8(out, 0, data, len);
    out[len * 2] = 0;
}

static bool hex_equals(const u8 *data, u32 len, const char *expected)
{
    char tmp[160];
    if (len * 2 + 1 > sizeof(tmp)) return false;
    to_hex(tmp, data, len);
    return xstricmp_ascii(tmp, expected) == 0;
}

// ---- self test ---------------------------------------------------------- //

static int run_selftest()
{
    int failures = 0;

    auto check = [&](bool ok, const char *name) {
        out_str(ok ? "PASS  " : "FAIL  ");
        say(name);
        if (!ok) ++failures;
    };

    u8 d[32];
    sha256("abc", 3, d);
    check(hex_equals(d, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "sha256(abc)");

    const char *msg = "The quick brown fox jumps over the lazy dog";
    hmac_sha256((const u8 *)"key", 3, (const u8 *)msg, 43, d);
    check(hex_equals(d, 32, "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"),
          "hmac-sha256(key)");

    pbkdf2_sha256("password", 8, (const u8 *)"salt", 4, 1, d);
    check(hex_equals(d, 32, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"),
          "pbkdf2 c=1");

    pbkdf2_sha256("password", 8, (const u8 *)"salt", 4, 2, d);
    check(hex_equals(d, 32, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"),
          "pbkdf2 c=2");

    {
        u8 key[32], pt[16], ct[16];
        for (int i = 0; i < 32; ++i) key[i] = (u8)i;
        for (int i = 0; i < 16; ++i) pt[i] = (u8)(i * 0x11);
        Aes256 aes;
        aes256_init(&aes, key);
        aes256_encrypt_block(&aes, pt, ct);
        check(hex_equals(ct, 16, "8ea2b7ca516745bfeafc49904b496089"), "aes-256 block (FIPS-197 C.3)");
    }

    {
        u8 key[32], nonce[12], tag[16];
        xmemset(key, 0, sizeof(key));
        xmemset(nonce, 0, sizeof(nonce));
        gcm_encrypt(key, nonce, (const u8 *)"", 0, nullptr, tag);
        check(hex_equals(tag, 16, "530f8afbc74536b9a963b4f1c4cb738b"), "gcm tag (empty pt)");
    }

    {
        u8 key[32], nonce[12], tag[16], ct[16], pt[16], back[16];
        xmemset(key, 0, sizeof(key));
        xmemset(nonce, 0, sizeof(nonce));
        xmemset(pt, 0, sizeof(pt));

        gcm_encrypt(key, nonce, pt, 16, ct, tag);
        check(hex_equals(ct, 16, "cea7403d4d606b6e074ec5d3baf39d18"), "gcm ct (16 zero bytes)");
        check(hex_equals(tag, 16, "d0d1c8a799996bf0265b98b5d48ab919"), "gcm tag (16 zero bytes)");
        check(gcm_decrypt(key, nonce, ct, 16, back, tag) && xmemcmp(back, pt, 16) == 0, "gcm round trip");

        tag[0] ^= 1;
        check(!gcm_decrypt(key, nonce, ct, 16, back, tag), "gcm rejects a modified tag");
    }

    {
        // LZ4: repetitive + pseudorandom buffers must survive a round trip.
        const u32 sizes[4] = { 1, 17, 4096, 70000 };
        bool allOk = true;

        for (int s = 0; s < 4 && allOk; ++s)
        {
            const u32 n = sizes[s];
            u8 *src = (u8 *)xalloc(n);
            u8 *packed = (u8 *)xalloc(lz4_bound(n));
            u8 *back = (u8 *)xalloc(n);
            if (!src || !packed || !back)
            {
                allOk = false;
            }
            else
            {
                for (u32 i = 0; i < n; ++i)
                {
                    src[i] = (u8)((i % 37 == 0) ? (i * 7) : ((s == 3) ? (i * 31 + 11) : 'A'));
                }

                const size_t packedLen = lz4_compress_block(src, n, packed, lz4_bound(n));
                const size_t backLen = packedLen ? lz4_decompress_block(packed, packedLen, back, n) : 0;
                allOk = packedLen != 0 && backLen == n && xmemcmp(src, back, n) == 0;
            }

            xfree(src);
            xfree(packed);
            xfree(back);
        }

        check(allOk, "lz4 round trip (1..70k)");
    }

    say("");
    say(failures == 0 ? "SELFTEST OK" : "SELFTEST FAILED");
    return failures == 0 ? 0 : 1;
}

// ---- info --------------------------------------------------------------- //

static int do_info(const char *path)
{
    PayloadInfo info;
    char err[160];

    if (!payload_probe(path, &info, err, sizeof(err)))
    {
        say2("error: ", err);
        return 1;
    }

    char line[512];
    size_t p = 0;

    p = fmt_str(line, p, "package     : ");
    p = fmt_str(line, p, path);
    line[p] = 0;
    say(line);

    p = 0;
    p = fmt_str(line, p, "content     : ");
    p = fmt_str(line, p, info.name);
    line[p] = 0;
    say(line);

    p = 0;
    p = fmt_str(line, p, "type        : ");
    p = fmt_str(line, p, (info.flags & kFlagFolder) ? "folder" : ((info.flags & kFlagText) ? "text" : "file"));
    p = fmt_str(line, p, (info.flags & kFlagCompressed) ? " (lz4)" : " (stored)");
    p = fmt_str(line, p, (info.flags & kFlagSelfDelete) ? " (self-delete)" : "");
    line[p] = 0;
    say(line);

    p = 0;
    p = fmt_str(line, p, "plain size  : ");
    p = fmt_size(line, p, info.plainLen);
    line[p] = 0;
    say(line);

    p = 0;
    p = fmt_str(line, p, "payload     : ");
    p = fmt_size(line, p, info.dataLen);
    line[p] = 0;
    say(line);

    p = 0;
    p = fmt_str(line, p, "cipher      : AES-256-GCM, PBKDF2-HMAC-SHA256 x");
    p = fmt_u64(line, p, info.iterations);
    line[p] = 0;
    say(line);

    p = 0;
    p = fmt_str(line, p, "salt        : ");
    p = fmt_hex8(line, p, info.salt, kSaltLen);
    line[p] = 0;
    say(line);

    p = 0;
    p = fmt_str(line, p, "nonce       : ");
    p = fmt_hex8(line, p, info.nonce, kNonceLen);
    line[p] = 0;
    say(line);

    return 0;
}

// ---- source dump (--src) ------------------------------------------------ //

static int do_dump_src(const char *dir)
{
#if !kHasSrcBlob
    say("error: this binary was built without the embedded source blob");
    return 1;
#else
    char err[160];

    u8 *raw = (u8 *)xalloc((size_t)g_src_blob_raw_len);
    if (!raw)
    {
        say("error: out of memory");
        return 1;
    }

    const size_t rawLen = lz4_decompress_block(g_src_blob, (size_t)g_src_blob_len,
                                               raw, (size_t)g_src_blob_raw_len);
    if (rawLen == 0)
    {
        xfree(raw);
        say("error: the embedded source blob is corrupted");
        return 1;
    }

    say2("writing capsaenc sources -> ", dir);
    if (!archive_extract(raw, rawLen, dir, true, err, sizeof(err)))
    {
        xfree(raw);
        say2("error: ", err);
        return 1;
    }

    xfree(raw);
    say("done - build with build.bat (Windows) or make (Linux)");
    return 0;
#endif
}

// ---- packing ------------------------------------------------------------ //

struct FolderScan
{
    u32 count;
    u64 dataBytes;
    u64 headerBytes;
    u8 *buffer;   // filled on the second pass
    u64 pos;
    const char *root;
    char prefix[kMaxNameBytes * 2 + 1];   // root folder name + '/' (archive paths carry it)
    u32 filesDone;                        // progress counters (second pass)
    u64 bytesDone;
    bool failed;
};

static bool count_entry(const WalkEntry &e, void *user)
{
    FolderScan *scan = (FolderScan *)user;
    const size_t nameLen = xstrlen(scan->prefix) + xstrlen(e.rel);

    ++scan->count;
    scan->headerBytes += 3 + nameLen;
    if (!e.is_dir)
    {
        scan->dataBytes += e.size;
        scan->headerBytes += 16;
    }

    return true;
}

static bool emit_entry(const WalkEntry &e, void *user)
{
    FolderScan *scan = (FolderScan *)user;

    char fullRel[kMaxNameBytes * 2 + 1];
    size_t np = fmt_str(fullRel, 0, scan->prefix);
    for (size_t k = 0; e.rel[k] && np < sizeof(fullRel) - 1; ++k) fullRel[np++] = e.rel[k];
    fullRel[np] = 0;
    const size_t nameLen = np;

    u8 *out = scan->buffer + scan->pos;
    out[0] = e.is_dir ? kEntryDir : kEntryFile;
    wr16(out + 1, (u16)nameLen);
    xmemcpy(out + 3, fullRel, nameLen);
    scan->pos += 3 + nameLen;

    if (e.is_dir) return true;

    char full[1024];
    path_join(full, sizeof(full), scan->root, e.rel);

    u8 *file = nullptr;
    u64 size = 0;
    if (!file_read_all(full, &file, &size))
    {
        scan->failed = true;
        return false;
    }

    u8 *body = scan->buffer + scan->pos;
    wr64(body, size);
    wr64(body + 8, e.mtime);
    scan->pos += 16;

    const u64 copy = size < e.size ? size : e.size;
    xmemcpy(scan->buffer + scan->pos, file, (size_t)copy);
    scan->pos += copy;
    xfree(file);

    // per-file progress (TTY only)
    ++scan->filesDone;
    scan->bytesDone += copy;
    {
        char line[128];
        size_t lp = fmt_str(line, 0, "reading ");
        lp = fmt_u64(line, lp, scan->filesDone);
        lp = fmt_str(line, lp, "/");
        lp = fmt_u64(line, lp, scan->count);
        lp = fmt_str(line, lp, " files  ");
        lp = fmt_size(line, lp, scan->bytesDone);
        lp = fmt_str(line, lp, " / ");
        lp = fmt_size(line, lp, scan->dataBytes);
        line[lp] = 0;
        status(line);
    }

    return true;
}

static int do_pack(int kind, const char *input, const char *outPath, u64 maxSize,
                   bool compress, bool selfDelete, bool passStdin)
{
    // kind: 0 = file, 1 = folder, 2 = text
    u8 *plain = nullptr;
    u64 plainSize = 0;
    char name[kMaxNameBytes + 1];
    xmemset(name, 0, sizeof(name));

    u8 flags = 0;

    if (kind == 0)
    {
        if (!file_read_all(input, &plain, &plainSize))
        {
            say("error: cannot read the input file");
            return 1;
        }
        xstrncpy(name, base_name(input), kMaxNameBytes);
    }
    else if (kind == 2)
    {
        const u64 len = xstrlen(input);
        plain = (u8 *)xalloc((size_t)(len ? len : 1));
        if (!plain) return 1;
        xmemcpy(plain, input, len);
        plainSize = len;
        flags |= kFlagText;
        xstrncpy(name, "text.txt", kMaxNameBytes);
    }
    else
    {
        FolderScan scan;
        xmemset(&scan, 0, sizeof(scan));
        scan.root = input;
        // Archive entries are stored under the folder's own name so that
        // extraction rebuilds the directory instead of spilling files.
        {
            size_t p = fmt_str(scan.prefix, 0, base_name(input));
            if (p && scan.prefix[p - 1] != '/') scan.prefix[p++] = '/';
            scan.prefix[p] = 0;
        }

        if (!walk_dir(input, count_entry, &scan) || scan.count == 0)
        {
            say("error: cannot read the folder (or it is empty)");
            return 1;
        }

        plainSize = 4 + scan.headerBytes + scan.dataBytes;
        plain = (u8 *)xalloc((size_t)plainSize);
        if (!plain)
        {
            say("error: out of memory");
            return 1;
        }

        scan.buffer = plain;
        scan.pos = 4;
        wr32(plain, scan.count);

        if (!walk_dir(input, emit_entry, &scan) || scan.failed)
        {
            xfree(plain);
            say("error: failed while reading the folder");
            return 1;
        }

        flags |= kFlagFolder;
        xstrncpy(name, base_name(input), kMaxNameBytes - 5);
        {
            const size_t n = xstrlen(name);
            xmemcpy(name + n, ".dir", 4);
            name[n + 4] = 0;
        }
    }

    if (selfDelete) flags |= kFlagSelfDelete;

    // ---- early size prediction ---------------------------------------------
    // Stub + header + tag + footer are known before any compression runs, so
    // the final package size can be predicted right after the input is read.
    const u64 overhead = (u64)g_stub_len + kHeaderFixed + (u64)xstrlen(name) + kTagLen + kFooterLen;
    const u64 rawTotal = overhead + plainSize;
    const bool canCompress = compress && plainSize > 32;

    if (maxSize && rawTotal > maxSize && !canCompress)
    {
        // Nothing can shrink this input - refuse before doing any work.
        char line[300];
        size_t p = fmt_str(line, 0, "error: package would be at least ");
        p = fmt_size(line, p, rawTotal);
        p = fmt_str(line, p, " (limit ");
        p = fmt_size(line, p, maxSize);
        p = fmt_str(line, p, ", content ");
        p = fmt_size(line, p, plainSize);
        p = fmt_str(line, p, " + stub/overhead ");
        p = fmt_size(line, p, overhead);
        p = fmt_str(line, p, "). Raise --max-size or split the input.");
        line[p] = 0;
        say(line);
        xfree(plain);
        return 1;
    }

    // ---- compression ----
    u8 *data = plain;
    u64 dataSize = plainSize;

    if (canCompress)
    {
        {
            char line[80];
            size_t p = fmt_str(line, 0, "compressing ");
            p = fmt_size(line, p, plainSize);
            p = fmt_str(line, p, " ...");
            line[p] = 0;
            status(line);
        }

        u8 *packed = (u8 *)xalloc(lz4_bound((size_t)plainSize));
        if (packed)
        {
            const size_t packedLen = lz4_compress_block(plain, (size_t)plainSize, packed, lz4_bound((size_t)plainSize));
            if (packedLen && packedLen < plainSize)
            {
                xfree(plain);
                data = packed;
                dataSize = packedLen;
                flags |= kFlagCompressed;
            }
            else
            {
                xfree(packed);
            }
        }
        status_end();
    }

    // ---- size check (before the password / encryption work) ----
    const u64 total = overhead + dataSize;

    if (maxSize && total > maxSize)
    {
        char line[300];
        size_t p = fmt_str(line, 0, "error: package would be ");
        p = fmt_size(line, p, total);
        p = fmt_str(line, p, " (limit ");
        p = fmt_size(line, p, maxSize);
        p = fmt_str(line, p, ", content ");
        p = fmt_size(line, p, plainSize);
        p = fmt_str(line, p, " -> ");
        p = fmt_size(line, p, dataSize);
        p = fmt_str(line, p, (flags & kFlagCompressed) ? " compressed, stub/overhead " : ", stub/overhead ");
        p = fmt_size(line, p, overhead);
        p = fmt_str(line, p, "). Raise --max-size or split the input.");
        line[p] = 0;
        say(line);
        xfree(data);   // data always owns the payload buffer at this point
        return 1;
    }

    // ---- password ----
    char pw[128], pw2[128];
    if (passStdin)
    {
        // One line from stdin, no prompts, no confirmation (scripting/testing).
        if (!read_password(nullptr, pw, sizeof(pw)) || pw[0] == 0)
        {
            say("error: empty password");
            return 1;
        }
    }
    else
    {
        if (!read_password("Password: ", pw, sizeof(pw)) || pw[0] == 0)
        {
            say("error: empty password");
            return 1;
        }
        if (!read_password("Repeat  : ", pw2, sizeof(pw2)))
        {
            return 1;
        }
        if (xstrcmp(pw, pw2) != 0)
        {
            say("error: passwords do not match");
            return 1;
        }
    }

    // ---- encrypt ----
    u8 salt[kSaltLen], nonce[kNonceLen];
    if (!random_bytes(salt, kSaltLen) || !random_bytes(nonce, kNonceLen))
    {
        say("error: cannot obtain random bytes");
        return 1;
    }

    {
        char line[80];
        size_t p = fmt_str(line, 0, "encrypting ");
        p = fmt_size(line, p, dataSize);
        p = fmt_str(line, p, " (PBKDF2 x");
        p = fmt_u64(line, p, kDefaultIterations);
        p = fmt_str(line, p, " + AES-256-GCM) ...");
        line[p] = 0;
        status(line);
    }

    u8 key[kKeyLen];
    pbkdf2_sha256(pw, xstrlen(pw), salt, kSaltLen, kDefaultIterations, key);

    u8 *cipher = (u8 *)xalloc((size_t)(dataSize ? dataSize : 1));
    u8 tag[kTagLen];
    if (!cipher)
    {
        say("error: out of memory");
        return 1;
    }

    gcm_encrypt(key, nonce, data, (size_t)dataSize, cipher, tag);
    xmemset(key, 0, sizeof(key));
    status_end();

    // ---- assemble ----
    const u32 nameLen = (u32)xstrlen(name);
    const u64 payloadSize = kHeaderFixed + nameLen + dataSize + kTagLen;

    u8 *out = (u8 *)xalloc((size_t)total);
    if (!out)
    {
        say("error: out of memory");
        return 1;
    }

    u64 pos = 0;
    xmemcpy(out, g_stub, g_stub_len);
    pos += g_stub_len;

    u8 *header = out + pos;
    xmemcpy(header + kMagicOffset, kMagic, 8);
    header[kFlagsOffset] = flags;
    header[9] = header[10] = header[11] = 0;
    wr32(header + kIterOffset, kDefaultIterations);
    xmemcpy(header + kSaltOffset, salt, kSaltLen);
    xmemcpy(header + kNonceOffset, nonce, kNonceLen);
    wr64(header + kPlainLenOffset, plainSize);
    wr64(header + kDataLenOffset, dataSize);
    wr16(header + kNameLenOffset, (u16)nameLen);
    xmemcpy(header + kHeaderFixed, name, nameLen);
    pos += kHeaderFixed + nameLen;

    xmemcpy(out + pos, cipher, (size_t)dataSize);
    pos += dataSize;
    xmemcpy(out + pos, tag, kTagLen);
    pos += kTagLen;

    u8 *footer = out + pos;
    wr64(footer + kFooterOffsetField, (u64)g_stub_len);
    wr64(footer + kFooterSizeField, payloadSize);
    xmemcpy(footer + kFooterMagicField, kFooterMagic, 8);
    pos += kFooterLen;

    if (!file_write_all(outPath, out, pos))
    {
        say("error: cannot write the output file");
        return 1;
    }

    char line[512];
    size_t p = 0;
    p = fmt_str(line, p, "packed  ");
    p = fmt_str(line, p, outPath);
    p = fmt_str(line, p, "  (");
    p = fmt_size(line, p, pos);
    p = fmt_str(line, p, ", payload ");
    p = fmt_size(line, p, dataSize);
    p = fmt_str(line, p, " from ");
    p = fmt_size(line, p, plainSize);
    p = fmt_str(line, p, (flags & kFlagCompressed) ? " compressed)" : " stored)");
    line[p] = 0;
    say(line);

    xfree(out);
    xfree(cipher);
    xfree(data);
    return 0;
}

// ---- command line ------------------------------------------------------- //

struct Args
{
    char *items[32];
    int count;

    const char *get(int i) const { return (i < count) ? items[i] : nullptr; }
};

static void split_args(const wchar_t *wide, Args *args, char *arena, size_t arenaCap)
{
    args->count = 0;
    size_t used = 0;
    const wchar_t *p = wide;

    while (*p && args->count < 32)
    {
        while (*p == L' ' || *p == L'\t') ++p;
        if (!*p) break;

        wchar_t token[1024];
        size_t n = 0;
        bool quoted = false;

        while (*p && (quoted || (*p != L' ' && *p != L'\t')))
        {
            if (*p == L'"')
            {
                quoted = !quoted;
                ++p;
                continue;
            }
            if (n + 1 < 1024) token[n++] = *p;
            ++p;
        }
        token[n] = 0;

        char utf8[2048];
        if (WideCharToMultiByte(CP_UTF8, 0, token, -1, utf8, (int)sizeof(utf8), nullptr, nullptr) <= 0)
        {
            continue;
        }

        const size_t len = xstrlen(utf8);
        if (used + len + 1 >= arenaCap) break;
        xmemcpy(arena + used, utf8, len + 1);
        args->items[args->count++] = arena + used;
        used += len + 1;
    }
}

static int app_main(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return 1;
    }

    const char *file = nullptr;
    const char *folder = nullptr;
    const char *text = nullptr;
    const char *out = nullptr;
    const char *infoPath = nullptr;
    u64 maxSize = kDefaultMaxSize;
    bool compress = true;
    bool selfDelete = false;
    bool passStdin = false;

    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];

        if (xstrcmp(a, "-h") == 0 || xstrcmp(a, "--help") == 0)
        {
            usage();
            return 0;
        }
        if (xstrcmp(a, "-v") == 0 || xstrcmp(a, "--version") == 0)
        {
            say("capsaenc " kVersion);
            return 0;
        }
        if (xstrcmp(a, "--selftest") == 0)
        {
            return run_selftest();
        }
        if (xstrcmp(a, "--info") == 0 && i + 1 < argc)
        {
            infoPath = argv[++i];
            continue;
        }
        if (xstrcmp(a, "--src") == 0)
        {
            const char *dir = "./capsaenc-src";
            if (i + 1 < argc && argv[i + 1][0] != '-') dir = argv[++i];
            return do_dump_src(dir);
        }
        if (xstrcmp(a, "-f") == 0 && i + 1 < argc)
        {
            file = argv[++i];
            continue;
        }
        if (xstrcmp(a, "-d") == 0 && i + 1 < argc)
        {
            folder = argv[++i];
            continue;
        }
        if (xstrcmp(a, "-t") == 0 && i + 1 < argc)
        {
            text = argv[++i];
            continue;
        }
        if (xstrcmp(a, "-o") == 0 && i + 1 < argc)
        {
            out = argv[++i];
            continue;
        }
        if (xstrcmp(a, "--max-size") == 0 && i + 1 < argc)
        {
            maxSize = parse_size(argv[++i]);
            continue;
        }
        if (xstrcmp(a, "--no-compress") == 0)
        {
            compress = false;
            continue;
        }
        if (xstrcmp(a, "--self-delete") == 0)
        {
            selfDelete = true;
            continue;
        }
        if (xstrcmp(a, "--pass-stdin") == 0)
        {
            passStdin = true;
            continue;
        }

        say2("error: unknown option ", a);
        return 1;
    }

    if (infoPath)
    {
        return do_info(infoPath);
    }

    int kind = -1;
    const char *input = nullptr;

    if (file) { kind = 0; input = file; }
    else if (folder) { kind = 1; input = folder; }
    else if (text) { kind = 2; input = text; }

    if (kind < 0)
    {
        say("error: one of -f / -d / -t is required");
        usage();
        return 1;
    }

    char outBuf[512];
    if (!out)
    {
        const char *base = (kind == 2) ? "text" : base_name(input);
        xstrncpy(outBuf, base, sizeof(outBuf) - 5);
        // strip a trailing extension, then add .exe
        char *dot = nullptr;
        for (char *p = outBuf; *p; ++p)
        {
            if (*p == '.') dot = p;
        }
        if (dot) *dot = 0;
        const size_t n = xstrlen(outBuf);
        xmemcpy(outBuf + n, ".exe", 5);
        out = outBuf;
    }

    return do_pack(kind, input, out, maxSize, compress, selfDelete, passStdin);
}

// ---- entry -------------------------------------------------------------- //

extern "C" void __stdcall mainCRTStartup(void)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    static char arena[8192];
    Args args;
    split_args(GetCommandLineW(), &args, arena, sizeof(arena));

    int code = app_main(args.count, args.items);
    ExitProcess((UINT)code);
}
