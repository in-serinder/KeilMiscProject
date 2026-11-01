// io_win.cpp - Win32 implementation (no CRT, no STL).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "io.h"

// ---- allocator ---------------------------------------------------------- //

void *xalloc(size_t size)
{
    if (size == 0) size = 1;
    void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    return p;
}

void xfree(void *p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

// ---- utf-8 <-> utf-16 --------------------------------------------------- //

static bool to_wide(const char *utf8, wchar_t *dst, int cap)
{
    if (!utf8) return false;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, cap);
    return n > 0;
}

static bool to_utf8(const wchar_t *wide, char *dst, int cap)
{
    if (!wide) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, dst, cap, nullptr, nullptr);
    return n > 0;
}

// ---- console ------------------------------------------------------------ //

static HANDLE out_handle() { return GetStdHandle(STD_OUTPUT_HANDLE); }
static HANDLE err_handle() { return GetStdHandle(STD_ERROR_HANDLE); }

static void write_to(HANDLE h, const char *s, size_t n)
{
    while (n)
    {
        DWORD chunk = (DWORD)(n > 0x40000000u ? 0x40000000u : n);
        DWORD wrote = 0;
        if (!WriteFile(h, s, chunk, &wrote, nullptr) || wrote == 0) return;
        s += wrote;
        n -= wrote;
    }
}

void out_raw(const char *s, size_t n) { write_to(out_handle(), s, n); }
void err_raw(const char *s, size_t n) { write_to(err_handle(), s, n); }

bool stdout_is_tty()
{
    DWORD mode = 0;
    return GetConsoleMode(out_handle(), &mode) != 0;
}

bool read_line(char *buf, size_t cap)
{
    if (cap == 0) return false;

    size_t len = 0;
    for (;;)
    {
        char c = 0;
        DWORD got = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &got, nullptr) || got == 0)
        {
            buf[len] = 0;
            return len > 0;
        }

        if (len == 0 && c == '\n') continue;   // leftover console '\n'

        if (c == '\r' || c == '\n')
        {
            buf[len] = 0;
            out_raw("\n", 1);
            return true;
        }

        if (c == 8 || c == 127)
        {
            if (len > 0)
            {
                --len;
                out_raw("\b \b", 3);
            }
            continue;
        }

        if (len + 1 < cap)
        {
            buf[len++] = c;
            out_raw(&c, 1);
        }
    }
}

bool read_password(const char *prompt, char *buf, size_t cap)
{
    if (cap == 0) return false;
    if (prompt) out_str(prompt);

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldMode = 0;
    bool tty = GetConsoleMode(in, &oldMode) != 0;
    if (tty) SetConsoleMode(in, oldMode & ~ENABLE_ECHO_INPUT);

    size_t len = 0;
    bool ok = true;

    for (;;)
    {
        char c = 0;
        DWORD got = 0;
        if (!ReadFile(in, &c, 1, &got, nullptr) || got == 0)
        {
            ok = false;
            break;
        }

        // Console line input queues "\r\n" for Enter; the previous call
        // consumed the '\r', so drop a leading leftover '\n' here.
        if (len == 0 && c == '\n') continue;

        if (c == '\r' || c == '\n')
        {
            out_raw("\n", 1);
            break;
        }

        if (c == 8 || c == 127)
        {
            if (len > 0)
            {
                --len;
                if (tty) out_raw("\b \b", 3);
            }
            continue;
        }

        if (len + 1 < cap)
        {
            buf[len++] = c;
            if (tty) out_raw("*", 1);
        }
    }

    if (tty) SetConsoleMode(in, oldMode);
    buf[len] = 0;
    return ok;
}

// ---- files -------------------------------------------------------------- //

bool file_exists(const char *path)
{
    wchar_t w[1024];
    if (!to_wide(path, w, 1024)) return false;
    DWORD attr = GetFileAttributesW(w);
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool file_read_all(const char *path, u8 **data, u64 *size)
{
    *data = nullptr;
    *size = 0;

    wchar_t w[1024];
    if (!to_wide(path, w, 1024)) return false;

    HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 0)
    {
        CloseHandle(h);
        return false;
    }

    u64 total = (u64)li.QuadPart;
    u8 *buf = (u8 *)xalloc((size_t)(total ? total : 1));
    if (!buf)
    {
        CloseHandle(h);
        return false;
    }

    u64 done = 0;
    while (done < total)
    {
        DWORD want = (DWORD)((total - done) > 0x40000000u ? 0x40000000u : (total - done));
        DWORD got = 0;
        if (!ReadFile(h, buf + done, want, &got, nullptr) || got == 0) break;
        done += got;
    }

    CloseHandle(h);
    *data = buf;
    *size = done;
    return done == total;
}

static bool write_handle_all(HANDLE h, const u8 *data, u64 size)
{
    u64 done = 0;
    while (done < size)
    {
        DWORD want = (DWORD)((size - done) > 0x40000000u ? 0x40000000u : (size - done));
        DWORD wrote = 0;
        if (!WriteFile(h, data + done, want, &wrote, nullptr) || wrote == 0) return false;
        done += wrote;
    }
    return true;
}

bool file_write_all(const char *path, const u8 *data, u64 size)
{
    wchar_t w[1024];
    if (!to_wide(path, w, 1024)) return false;

    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = write_handle_all(h, data, size);
    CloseHandle(h);
    return ok;
}

bool file_write_all_progress(const char *path, const u8 *data, u64 size, ProgressFn fn, void *user)
{
    wchar_t w[1024];
    if (!to_wide(path, w, 1024)) return false;

    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const u64 chunk = 256 * 1024;
    u64 done = 0;
    bool ok = true;

    while (done < size)
    {
        const u64 want = (size - done) < chunk ? (size - done) : chunk;
        DWORD wrote = 0;
        if (!WriteFile(h, data + done, (DWORD)want, &wrote, nullptr) || wrote == 0)
        {
            ok = false;
            break;
        }
        done += wrote;
        if (fn) fn(done, size, user);
    }

    CloseHandle(h);
    return ok;
}

bool file_write_at(const char *path, u64 offset, const u8 *data, u64 size)
{
    wchar_t w[1024];
    if (!to_wide(path, w, 1024)) return false;

    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    bool ok = SetFilePointerEx(h, li, nullptr, FILE_BEGIN) != 0 && write_handle_all(h, data, size);
    CloseHandle(h);
    return ok;
}

bool dir_create(const char *path)
{
    wchar_t w[1024];
    if (!to_wide(path, w, 1024)) return false;
    if (CreateDirectoryW(w, nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool dir_create_for_file(const char *path)
{
    // Creates every directory component of `path` (the last component is the file).
    char tmp[1024];
    xstrncpy(tmp, path, sizeof(tmp));

    for (size_t i = 1; tmp[i]; ++i)
    {
        if (tmp[i] != '\\' && tmp[i] != '/') continue;
        char saved = tmp[i];
        tmp[i] = 0;
        if (!dir_create(tmp))
        {
            tmp[i] = saved;
            return false;
        }
        tmp[i] = saved;
    }
    return true;
}

bool self_path(char *buf, size_t cap)
{
    wchar_t w[1024];
    DWORD n = GetModuleFileNameW(nullptr, w, 1024);
    if (n == 0 || n >= 1024) return false;
    return to_utf8(w, buf, (int)cap);
}

void delete_self(const char *path)
{
    wchar_t wpath[1024];
    if (!to_wide(path, wpath, 1024)) return;

    wchar_t cmd[2048];
    const wchar_t *prefix = L"cmd.exe /c ping -n 2 127.0.0.1 >nul & del /f /q \"";
    size_t i = 0;
    while (prefix[i]) { cmd[i] = prefix[i]; ++i; }
    for (size_t k = 0; wpath[k] && i + 2 < 2048; ++k) cmd[i++] = wpath[k];
    cmd[i++] = '"';
    cmd[i] = 0;

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    xmemset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    xmemset(&pi, 0, sizeof(pi));

    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// ---- directory walking -------------------------------------------------- //

static bool walk_inner(const wchar_t *rootW, const wchar_t *relW, WalkFn fn, void *user)
{
    wchar_t pattern[2048];
    size_t i = 0;
    for (size_t k = 0; rootW[k] && i + 2 < 2048; ++k) pattern[i++] = rootW[k];
    if (relW && relW[0])
    {
        if (i && pattern[i - 1] != L'\\') pattern[i++] = L'\\';
        for (size_t k = 0; relW[k] && i + 2 < 2048; ++k) pattern[i++] = relW[k];
    }
    if (i && pattern[i - 1] != L'\\') pattern[i++] = L'\\';
    pattern[i++] = L'*';
    pattern[i] = 0;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return true;

    bool ok = true;
    do
    {
        if (xwcscmp(fd.cFileName, L".") == 0 || xwcscmp(fd.cFileName, L"..") == 0) continue;

        wchar_t relChild[2048];
        size_t j = 0;
        if (relW && relW[0])
        {
            for (size_t k = 0; relW[k] && j + 2 < 2048; ++k) relChild[j++] = relW[k];
            relChild[j++] = L'/';
        }
        for (size_t k = 0; fd.cFileName[k] && j + 1 < 2048; ++k) relChild[j++] = fd.cFileName[k];
        relChild[j] = 0;

        WalkEntry e;
        e.is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = ((u64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;

        // FILETIME (100 ns since 1601) -> milliseconds since the Unix epoch.
        const u64 ft = ((u64)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        e.mtime = (ft >= 116444736000000000ULL) ? (ft - 116444736000000000ULL) / 10000ULL : 0;

        if (!to_utf8(relChild, e.rel, (int)sizeof(e.rel)))
        {
            continue;
        }

        if (!fn(e, user))
        {
            ok = false;
            break;
        }

        if (e.is_dir && !walk_inner(rootW, relChild, fn, user))
        {
            ok = false;
            break;
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return ok;
}

bool walk_dir(const char *root, WalkFn fn, void *user)
{
    wchar_t w[1024];
    if (!to_wide(root, w, 1024)) return false;
    return walk_inner(w, L"", fn, user);
}

bool file_set_mtime(const char *path, u64 mtimeMs)
{
    wchar_t w[1024];
    if (!to_wide(path, w, 1024)) return false;

    HANDLE h = CreateFileW(w, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const u64 ft = mtimeMs * 10000ULL + 116444736000000000ULL;
    FILETIME ftime;
    ftime.dwLowDateTime = (DWORD)(ft & 0xFFFFFFFFu);
    ftime.dwHighDateTime = (DWORD)(ft >> 32);

    const bool ok = SetFileTime(h, nullptr, nullptr, &ftime) != 0;
    CloseHandle(h);
    return ok;
}

// ---- small helpers ------------------------------------------------------ //

char path_sep() { return '\\'; }

void path_join(char *dst, size_t cap, const char *a, const char *b)
{
    size_t i = 0;
    for (; a[i] && i + 1 < cap; ++i) dst[i] = a[i];
    if (i && dst[i - 1] != '\\' && dst[i - 1] != '/' && i + 1 < cap) dst[i++] = '\\';
    for (size_t k = 0; b[k] && i + 1 < cap; ++k) dst[i++] = b[k];
    dst[i] = 0;
}
