# CapasaEnc

A lightweight encrypted self-extracting packer in freestanding C++17 —
no CRT, no STL, both the packer and the packages it produces are capped at
**120 KB**.

```
file / text / folder  --[LZ4]-->  --[AES-256-GCM]-->  stub + payload  =  package.exe
```

- **Packer** `capsaenc.exe` (~54 KB) packs a file, a text string or a whole
  folder into a runnable self-extracting executable.
- **Package** (~25 KB stub + payload) prompts for a password, decrypts and
  extracts with an ASCII progress bar, `y/n/c` overwrite prompts and
  optional self-deletion.
- **Crypto**: PBKDF2-HMAC-SHA256 (100k iterations, 16 B salt) + AES-256-GCM
  (12 B nonce), LZ4 block compression, OS CSPRNG. All primitives implemented
  from scratch and verified against NIST test vectors via `--selftest`.
- **Features**: `--info` header dump, `--src` source self-dump, `--max-size`
  cap, `--self-delete`, `--pass-stdin` scripting mode, zip-slip protection,
  empty-folder preservation, file mtime restoration.

## Layout

```
src/            portable C++17 sources (sha256, aes-gcm, lz4, payload, stub, cli)
src/io_win.cpp  Win32 backend   src/io_posix.cpp  POSIX backend
tools/embed.py  embeds the built stub into the packer as a C array
tools/checksize.py  asserts the 120 KB size cap
build.bat       Windows build (MSVC, /NODEFAULTLIB, ml64 chkstk)
Makefile        Linux build (g++, links glibc)
docs/USAGE.md   full usage reference
```

## Quick start (Windows)

```
build.bat
build\capsaenc.exe --selftest
build\capsaenc.exe -f report.pdf -o share.exe
:: hand share.exe + the password to the recipient
share.exe            :: asks for a password, extracts into the current dir
```

Linux: `make && make selftest && make e2e`.

See [docs/USAGE.md](docs/USAGE.md) for the complete option list.
