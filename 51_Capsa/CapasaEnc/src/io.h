// io.h - the only platform-specific surface (Windows / POSIX).
#pragma once

#include "common.h"

// ---- console ------------------------------------------------------------ //

void out_raw(const char *s, size_t n);
void err_raw(const char *s, size_t n);

inline void out_str(const char *s) { out_raw(s, xstrlen(s)); }
inline void err_str(const char *s) { err_raw(s, xstrlen(s)); }

// Reads a password from the terminal, echoing '*' per character.
// Returns false when stdin is not a terminal (caller may fall back to plain read).
bool read_password(const char *prompt, char *buf, size_t cap);

// Plain line read without echo control (used for y/n answers).
bool read_line(char *buf, size_t cap);

bool stdout_is_tty();

// ---- files -------------------------------------------------------------- //

// Reads the whole file into a freshly allocated buffer (caller frees with xfree).
bool file_read_all(const char *path, u8 **data, u64 *size);

bool file_write_all(const char *path, const u8 *data, u64 size);

typedef void (*ProgressFn)(u64 done, u64 total, void *user);

// Same as file_write_all but reports progress in chunks (used by the stub).
bool file_write_all_progress(const char *path, const u8 *data, u64 size, ProgressFn fn, void *user);

bool file_exists(const char *path);

// Appends `size` bytes at the given offset, creating the file when needed.
bool file_write_at(const char *path, u64 offset, const u8 *data, u64 size);

bool dir_create(const char *path);

// Creates every missing directory component of `path` (path may be a file path).
bool dir_create_for_file(const char *path);

// Absolute path of the running executable.
bool self_path(char *buf, size_t cap);

// Best-effort removal of the running executable (deferred on Windows).
void delete_self(const char *path);

// ---- directory walking (folder encryption) ------------------------------ //

struct WalkEntry
{
    char rel[kMaxNameBytes * 2]; // relative path, '/' separated, UTF-8
    u64 size;
    u64 mtime;
    bool is_dir;
};

typedef bool (*WalkFn)(const WalkEntry &entry, void *user);

// Recursively lists `root` (paths relative to it). Return false from the
// callback to abort the walk.
bool walk_dir(const char *root, WalkFn fn, void *user);

// Sets the modification time of a file (milliseconds since the Unix epoch).
// Best effort: a false return is not fatal.
bool file_set_mtime(const char *path, u64 mtimeMs);

// ---- small helpers ------------------------------------------------------ //

char path_sep();
void path_join(char *dst, size_t cap, const char *a, const char *b);
