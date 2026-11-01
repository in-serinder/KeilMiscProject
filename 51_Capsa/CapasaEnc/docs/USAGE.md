# capsaenc - usage

A tiny encrypted self-extracting packer (AES-256-GCM + LZ4 + PBKDF2).
Both the packer and the packages it produces stay under the 120 KB size cap.

## Packing

```
capsaenc -f <file>     [-o out.exe] [options]    # encrypt a file
capsaenc -d <folder>   [-o out.exe] [options]    # encrypt a folder (recursive)
capsaenc -t <text>     [-o out.exe] [options]    # encrypt a text string
```

Options:

| Option            | Meaning                                                        |
|-------------------|----------------------------------------------------------------|
| `-o <path>`       | Output package path. Default: `<input base>.exe` / `text.exe`  |
| `--max-size <n>`  | Size cap for the produced package (default `120K`; `0` = off)  |
| `--no-compress`   | Skip the LZ4 pass (incompressible data)                        |
| `--self-delete`   | The package deletes itself after a successful extraction       |
| `--pass-stdin`    | Read the password from stdin once (no prompt, no confirmation) |
| `--info <package>`| Print a package's header and exit                              |
| `--src [dir]`     | Write capsaenc's own source code into dir (default `./capsaenc-src`) |
| `--selftest`      | Run the built-in crypto/LZ4 self test                          |
| `-v`              | Print the version                                              |

You will be prompted for the password twice (once with `--pass-stdin`).
The package embeds a ~25 KB extractor stub, so even a small file produces
a runnable executable.

While packing, a TTY console shows a live status line (files read, then
compressing / encrypting stages); piped or redirected output stays clean.

Size accounting: `package = stub (~25 KB) + encrypted payload + header`.
The final size is predicted **before** you are asked for a password - if the
input cannot fit (or compression does not shrink it below the cap), the tool
refuses immediately without doing the key-derivation / encryption work.

## Extracting

Run the package itself:

```
pack.exe                  # extract into the current directory
pack.exe -o <dir>         # extract into <dir>
pack.exe -y               # overwrite existing files without asking
pack.exe --info           # print the package header
pack.exe --delete         # delete the package after extraction
pack.exe --no-save        # text payloads: print only, do not write <name>.txt
pack.exe -h               # help
```

Behaviour:

- **File payloads** are written next to the target directory as their original
  name. If the target exists you are asked `y` (overwrite) / `n` (skip) /
  `c` (cancel the whole run).
- **Text payloads** are printed to the console and also saved as `text.txt`
  unless `--no-save` is given.
- **Folder payloads** rebuild the original folder (including empty
  sub-folders) under the target directory and restore file modification
  times where the OS allows.
- Wrong password or corrupted payload is reported as
  `error: wrong password or the payload was modified` (GCM tag check).

## Cryptography

- Key derivation: PBKDF2-HMAC-SHA256, 100000 iterations, 16-byte random salt
- Cipher: AES-256-GCM with a 12-byte random nonce
- Compression: LZ4 block format (skipped automatically when it does not help)
- Randomness: OS CSPRNG (`CryptGenRandom` / `/dev/urandom`)

## Building

Windows (MSVC 2022, freestanding - no CRT, no STL):

```
build.bat        # produces build\capsaenc.exe and build\stub.exe
```

Linux (g++ / clang, source-only delivery):

```
make             # produces build/capsaenc and build/stub + size check
make selftest
make e2e
```

The packer embeds its own source (LZ4-compressed, generated files excluded):
`capsaenc --src [dir]` writes a ready-to-build copy of `src/` plus a pointer
to the build scripts. Generated headers (`stub_blob.h`, `src_blob.h`) are
re-created by the build, so the dumped tree builds out of the box.

## Notes

- The archive stores paths relative to the packed folder; absolute paths and
  `..` components are rejected on extraction (zip-slip protection).
- `--self-delete` on Windows uses the delayed `cmd.exe /c ping ... & del`
  trick so the running image can be removed.
- Extraction progress is shown as an ASCII bar when stdout is a terminal.
