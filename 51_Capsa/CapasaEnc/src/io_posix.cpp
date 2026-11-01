// io_posix.cpp - Linux / POSIX implementation (libc is allowed here).
#include "io.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

// ---- allocator ---------------------------------------------------------- //

void *xalloc(size_t size)
{
    if (size == 0) size = 1;
    void *p = calloc(1, size);
    return p;
}

void xfree(void *p)
{
    free(p);
}

// ---- console ------------------------------------------------------------ //

void out_raw(const char *s, size_t n)
{
    while (n)
    {
        ssize_t wrote = write(STDOUT_FILENO, s, n);
        if (wrote <= 0) return;
        s += wrote;
        n -= (size_t)wrote;
    }
}

void err_raw(const char *s, size_t n)
{
    while (n)
    {
        ssize_t wrote = write(STDERR_FILENO, s, n);
        if (wrote <= 0) return;
        s += wrote;
        n -= (size_t)wrote;
    }
}

bool stdout_is_tty() { return isatty(STDOUT_FILENO) != 0; }

bool read_line(char *buf, size_t cap)
{
    if (cap == 0) return false;
    size_t len = 0;

    for (;;)
    {
        char c = 0;
        const ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0)
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

    const bool tty = isatty(STDIN_FILENO) != 0;
    termios oldState;
    if (tty && tcgetattr(STDIN_FILENO, &oldState) == 0)
    {
        termios quiet = oldState;
        quiet.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }

    size_t len = 0;
    bool ok = true;

    for (;;)
    {
        char c = 0;
        const ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0)
        {
            ok = false;
            break;
        }

        // A previous call may have left a console '\n' in the input queue.
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

    if (tty && tcgetattr(STDIN_FILENO, &oldState) == 0)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldState);
    }

    buf[len] = 0;
    return ok;
}

// ---- files -------------------------------------------------------------- //

bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

bool file_read_all(const char *path, u8 **data, u64 *size)
{
    *data = nullptr;
    *size = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return false;
    }

    const long len = ftell(fp);
    if (len < 0)
    {
        fclose(fp);
        return false;
    }
    rewind(fp);

    u8 *buf = (u8 *)xalloc((size_t)len + 1);
    if (!buf)
    {
        fclose(fp);
        return false;
    }

    const size_t got = fread(buf, 1, (size_t)len, fp);
    fclose(fp);

    *data = buf;
    *size = got;
    return got == (size_t)len;
}

static bool write_chunks(FILE *fp, const u8 *data, u64 size, ProgressFn fn, void *user)
{
    const u64 chunk = 256 * 1024;
    u64 done = 0;

    while (done < size)
    {
        const u64 want = (size - done) < chunk ? (size - done) : chunk;
        if (fwrite(data + done, 1, (size_t)want, fp) != (size_t)want) return false;
        done += want;
        if (fn) fn(done, size, user);
    }
    return true;
}

bool file_write_all(const char *path, const u8 *data, u64 size)
{
    return file_write_all_progress(path, data, size, nullptr, nullptr);
}

bool file_write_all_progress(const char *path, const u8 *data, u64 size, ProgressFn fn, void *user)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    const bool ok = write_chunks(fp, data, size, fn, user);
    fclose(fp);
    return ok;
}

bool file_write_at(const char *path, u64 offset, const u8 *data, u64 size)
{
    FILE *fp = fopen(path, "r+b");
    if (!fp) fp = fopen(path, "wb");
    if (!fp) return false;

    if (fseek(fp, (long)offset, SEEK_SET) != 0)
    {
        fclose(fp);
        return false;
    }

    const bool ok = fwrite(data, 1, (size_t)size, fp) == (size_t)size;
    fclose(fp);
    return ok;
}

bool dir_create(const char *path)
{
    if (mkdir(path, 0755) == 0) return true;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool dir_create_for_file(const char *path)
{
    char tmp[1024];
    xstrncpy(tmp, path, sizeof(tmp));

    for (size_t i = 1; tmp[i]; ++i)
    {
        if (tmp[i] != '/') continue;
        const char saved = tmp[i];
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
    const ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
    if (n <= 0) return false;
    buf[n] = 0;
    return true;
}

void delete_self(const char *path)
{
    unlink(path);
}

// ---- directory walking -------------------------------------------------- //

static bool walk_inner(const char *root, const char *rel, WalkFn fn, void *user)
{
    char dirPath[1024];
    if (rel && rel[0])
    {
        path_join(dirPath, sizeof(dirPath), root, rel);
    }
    else
    {
        xstrncpy(dirPath, root, sizeof(dirPath));
    }

    DIR *dir = opendir(dirPath);
    if (!dir) return false;

    bool ok = true;
    dirent *entry = nullptr;

    while ((entry = readdir(dir)) != nullptr)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        WalkEntry e;
        xmemset(&e, 0, sizeof(e));

        size_t p = 0;
        if (rel && rel[0])
        {
            p = fmt_str(e.rel, 0, rel);
            e.rel[p++] = '/';
        }
        xstrncpy(e.rel + p, entry->d_name, sizeof(e.rel) - p);

        char full[1024];
        path_join(full, sizeof(full), root, e.rel);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        e.is_dir = S_ISDIR(st.st_mode);
        e.size = (u64)st.st_size;
        e.mtime = (u64)st.st_mtime * 1000ULL;   // seconds -> milliseconds

        if (!fn(e, user))
        {
            ok = false;
            break;
        }

        if (e.is_dir && !walk_inner(root, e.rel, fn, user))
        {
            ok = false;
            break;
        }
    }

    closedir(dir);
    return ok;
}

bool walk_dir(const char *root, WalkFn fn, void *user)
{
    return walk_inner(root, "", fn, user);
}

bool file_set_mtime(const char *path, u64 mtimeMs)
{
    struct timespec times[2];
    times[0].tv_sec = (time_t)(mtimeMs / 1000ULL);
    times[0].tv_nsec = (long)((mtimeMs % 1000ULL) * 1000000ULL);
    times[1] = times[0];
    return utimensat(AT_FDCWD, path, times, 0) == 0;
}

// ---- small helpers ------------------------------------------------------ //

char path_sep() { return '/'; }

void path_join(char *dst, size_t cap, const char *a, const char *b)
{
    size_t i = 0;
    for (; a[i] && i + 2 < cap; ++i) dst[i] = a[i];
    if (i && dst[i - 1] != '/' && i + 2 < cap) dst[i++] = '/';
    for (size_t k = 0; b[k] && i + 1 < cap; ++k) dst[i++] = b[k];
    dst[i] = 0;
}
