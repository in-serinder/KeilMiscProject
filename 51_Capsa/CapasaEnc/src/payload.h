// payload.h - reading/decrypting a self-extracting package (shared by CLI and stub).
#pragma once

#include "format.h"

struct PayloadInfo
{
    u8 flags;
    u32 iterations;
    u8 salt[kSaltLen];
    u8 nonce[kNonceLen];
    u64 plainLen;
    u64 dataLen;
    char name[kMaxNameBytes + 1];
    u64 payloadOffset; // absolute offset of HEADER in the executable
};

// Parses the trailer + header of `exePath` without touching the password.
bool payload_probe(const char *exePath, PayloadInfo *info, char *err, size_t errCap);

// Reads, decrypts (and decompresses) the payload. `plain` is xalloc'ed.
bool payload_load(const char *exePath, const char *password, u8 **plain, u64 *plainSize,
                  PayloadInfo *info, char *err, size_t errCap);

// Extracts a folder archive (`plain` must hold the archive bytes).
bool archive_extract(const u8 *plain, u64 size, const char *outDir, bool assumeYes,
                     char *err, size_t errCap);
