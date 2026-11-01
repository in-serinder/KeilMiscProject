// format.h - on-disk layout shared by the encryptor and the SFX stub.
//
//   [stub binary][HEADER][name][ciphertext][tag][FOOTER]
//
// The stub locates the payload through FOOTER (fixed 24 bytes at end of file),
// so the same binary works no matter how large the payload is.
#pragma once

#include "common.h"

static constexpr u32 kDefaultIterations = 100000;
static constexpr u32 kSaltLen = 16;
static constexpr u32 kNonceLen = 12;
static constexpr u32 kTagLen = 16;
static constexpr u32 kKeyLen = 32;

// header
static constexpr u32 kHeaderFixed = 62;
static constexpr u32 kMagicOffset = 0;
static constexpr u32 kFlagsOffset = 8;
static constexpr u32 kIterOffset = 12;
static constexpr u32 kSaltOffset = 16;
static constexpr u32 kNonceOffset = 32;
static constexpr u32 kPlainLenOffset = 44;
static constexpr u32 kDataLenOffset = 52;
static constexpr u32 kNameLenOffset = 60;

// footer
static constexpr u32 kFooterLen = 24;
static constexpr u32 kFooterOffsetField = 0;
static constexpr u32 kFooterSizeField = 8;
static constexpr u32 kFooterMagicField = 16;

// flags
static constexpr u8 kFlagCompressed = 0x01;
static constexpr u8 kFlagText = 0x02;
static constexpr u8 kFlagFolder = 0x04;
static constexpr u8 kFlagSelfDelete = 0x08;

// archive entry types (inside the compressed/encrypted payload)
static constexpr u8 kEntryFile = 0;
static constexpr u8 kEntryDir = 1;

static const u8 kMagic[8] = { 'C', 'E', 'N', 'C', 'F', 'M', 'T', '1' };
static const u8 kFooterMagic[8] = { 'C', 'E', 'N', 'C', 'F', 'T', 'R', '1' };

// Default cap for a produced self-extracting program.
static constexpr u64 kDefaultMaxSize = 120u * 1024u;
