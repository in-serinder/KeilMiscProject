// stub.cpp - the self-extracting stub (both platforms, entry points at the bottom).
#include "format.h"
#include "io.h"
#include "payload.h"

// ---- helpers ------------------------------------------------------------ //

static void say(const char *s) { out_str(s); out_str("\n"); }

static void say2(const char *a, const char *b)
{
    out_str(a);
    out_str(b);
    out_str("\n");
}

static void say_err(const char *s)
{
    err_str("error: ");
    err_str(s);
    err_str("\n");
}

static void bar(u64 done, u64 total)
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
    if (percent < 100) line[p++] = ' ';
    if (percent < 10) line[p++] = ' ';
    p = fmt_u64(line, p, percent);
    p = fmt_str(line, p, "%  ");
    p = fmt_size(line, p, done);
    p = fmt_str(line, p, " / ");
    p = fmt_size(line, p, total);
    line[p++] = '\r';
    out_raw(line, p);
}

static void bar_done() { out_str("\n"); }

static void write_progress(u64 done, u64 total, void *user)
{
    (void)user;
    bar(done, total);
}

// ---- info --------------------------------------------------------------- //

static int show_info(const PayloadInfo &info)
{
    char line[512];
    size_t p = 0;

    p = fmt_str(line, p, "content     : ");
    p = fmt_str(line, p, info.name);
    line[p] = 0;
    say(line);

    p = 0;
    p = fmt_str(line, p, "type        : ");
    p = fmt_str(line, p, (info.flags & kFlagFolder) ? "folder" : ((info.flags & kFlagText) ? "text" : "file"));
    p = fmt_str(line, p, (info.flags & kFlagCompressed) ? " (lz4)" : " (stored)");
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
    p = fmt_str(line, p, "kdf         : PBKDF2-HMAC-SHA256 x");
    p = fmt_u64(line, p, info.iterations);
    line[p] = 0;
    say(line);
    return 0;
}

// ---- main --------------------------------------------------------------- //

static int sfx_run(const char *selfPath, int argc, char **argv)
{
    const char *outDir = ".";
    bool forceDelete = false;
    bool noSave = false;
    bool infoOnly = false;
    bool assumeYes = false;

    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        if (xstrcmp(a, "-h") == 0 || xstrcmp(a, "--help") == 0)
        {
            say("self-extracting package (AES-256-GCM)");
            say("");
            say("Usage: <package> [options]");
            say("  -o <dir>      extract into <dir> (default: current directory)");
            say("  -y            overwrite existing files without asking");
            say("  --info        print the package header and exit");
            say("  --delete      delete the package after extraction");
            say("  --no-save     for text payloads: print only, do not write .txt");
            return 0;
        }
        if (xstrcmp(a, "--info") == 0) { infoOnly = true; continue; }
        if (xstrcmp(a, "--delete") == 0) { forceDelete = true; continue; }
        if (xstrcmp(a, "--no-save") == 0) { noSave = true; continue; }
        if (xstrcmp(a, "-y") == 0) { assumeYes = true; continue; }
        if (xstrcmp(a, "-o") == 0 && i + 1 < argc) { outDir = argv[++i]; continue; }

        say2("error: unknown option ", a);
        return 1;
    }

    PayloadInfo info;
    char err[160];

    if (!payload_probe(selfPath, &info, err, sizeof(err)))
    {
        say_err(err);
        return 1;
    }

    if (infoOnly)
    {
        return show_info(info);
    }

    char pw[128];
    if (!read_password("Password: ", pw, sizeof(pw)) || pw[0] == 0)
    {
        say_err("empty password");
        return 1;
    }

    u8 *plain = nullptr;
    u64 plainSize = 0;

    if (!payload_load(selfPath, pw, &plain, &plainSize, &info, err, sizeof(err)))
    {
        xmemset(pw, 0, sizeof(pw));
        say_err(err);
        return 1;
    }
    xmemset(pw, 0, sizeof(pw));

    int status = 0;

    if (info.flags & kFlagFolder)
    {
        char line[256];
        size_t p = 0;
        p = fmt_str(line, p, "extracting ");
        p = fmt_str(line, p, info.name);
        p = fmt_str(line, p, " -> ");
        p = fmt_str(line, p, outDir);
        line[p] = 0;
        say(line);

        if (!archive_extract(plain, plainSize, outDir, assumeYes, err, sizeof(err)))
        {
            say_err(err);
            status = 1;
        }
    }
    else if (info.flags & kFlagText)
    {
        // print then save as .txt
        out_raw((const char *)plain, (size_t)plainSize);
        out_str("\n");

        if (!noSave)
        {
            char target[1024];
            path_join(target, sizeof(target), outDir, info.name);
            if (!file_exists(target) || assumeYes)
            {
                dir_create_for_file(target);
                bar(0, plainSize);
                if (file_write_all_progress(target, plain, plainSize, write_progress, nullptr))
                {
                    bar_done();
                    say2("saved ", target);
                }
                else
                {
                    bar_done();
                    say_err("cannot write the text file");
                    status = 1;
                }
            }
            else
            {
                say2("skipped (exists) ", target);
            }
        }
    }
    else
    {
        char target[1024];
        path_join(target, sizeof(target), outDir, info.name);

        bool write = true;
        if (file_exists(target) && !assumeYes)
        {
            out_str("exists: ");
            out_str(target);
            out_str("  [y]overwrite / [n]skip / [c]cancel > ");

            char answer[8];
            read_line(answer, sizeof(answer));
            if (answer[0] == 'c' || answer[0] == 'C')
            {
                xfree(plain);
                say("cancelled");
                return 1;
            }
            write = (answer[0] == 'y' || answer[0] == 'Y');
        }

        if (!write)
        {
            say2("skipped (exists) ", target);
        }
        else
        {
            dir_create_for_file(target);
            bar(0, plainSize);
            if (!file_write_all_progress(target, plain, plainSize, write_progress, nullptr))
            {
                bar_done();
                say_err("cannot write the output file");
                status = 1;
            }
            else
            {
                bar_done();
                char line[512];
                size_t p = 0;
                p = fmt_str(line, p, "extracted ");
                p = fmt_size(line, p, plainSize);
                p = fmt_str(line, p, " -> ");
                p = fmt_str(line, p, target);
                line[p] = 0;
                say(line);
            }
        }
    }

    xfree(plain);

    if (status == 0 && (forceDelete || (info.flags & kFlagSelfDelete)))
    {
        delete_self(selfPath);
    }

    return status;
}

// ---- entry points ------------------------------------------------------- //

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static void split(int *argc, char **argv, char *arena, size_t arenaCap)
{
    const wchar_t *p = GetCommandLineW();
    *argc = 0;
    size_t used = 0;

    while (*p && *argc < 16)
    {
        while (*p == L' ' || *p == L'\t') ++p;
        if (!*p) break;

        wchar_t token[1024];
        size_t n = 0;
        bool quoted = false;
        while (*p && (quoted || (*p != L' ' && *p != L'\t')))
        {
            if (*p == L'"') { quoted = !quoted; ++p; continue; }
            if (n + 1 < 1024) token[n++] = *p;
            ++p;
        }
        token[n] = 0;

        char utf8[2048];
        if (WideCharToMultiByte(CP_UTF8, 0, token, -1, utf8, (int)sizeof(utf8), nullptr, nullptr) <= 0) continue;

        const size_t len = xstrlen(utf8);
        if (used + len + 1 >= arenaCap) break;
        xmemcpy(arena + used, utf8, len + 1);
        argv[(*argc)++] = arena + used;
        used += len + 1;
    }
}

extern "C" void __stdcall sfx_entry(void)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    static char arena[4096];
    char *argv[16];
    int argc = 0;
    split(&argc, argv, arena, sizeof(arena));

    char self[1024];
    if (!self_path(self, sizeof(self)))
    {
        say_err("cannot locate the running executable");
        ExitProcess(1);
    }

    ExitProcess((UINT)sfx_run(self, argc, argv));
}

#else

#include <unistd.h>

int main(int argc, char **argv)
{
    char self[1024];
    if (!self_path(self, sizeof(self)))
    {
        say_err("cannot locate the running executable");
        return 1;
    }
    return sfx_run(self, argc, argv);
}

#endif
