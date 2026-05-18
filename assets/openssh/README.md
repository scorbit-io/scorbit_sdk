# Cross-built OpenSSH with PKCS#11

Build portable OpenSSH in Docker (`dilshodm/gcc-builder`, same as the SDK) for **armhf** and **arm64**. The client supports `PKCS11Provider` (ATECC via `libcryptoauth.so`).

## Build

```bash
make openssh armhf
make openssh arm64
```

Version is pinned in [`OPENSSH_VERSION`](../../OPENSSH_VERSION) (currently 10.0p1). Artifacts:

```text
build/openssh_armhf_u12/dist/openssh-<ver>-armhf.tar.gz
build/openssh_arm64_u18/dist/openssh-<ver>-arm64.tar.gz
```

Unpack on the device under `/` (contains `usr/bin/ssh`, `etc/ssh/`, …).

Build **after** the sysroot has OpenSSL (same gcc-builder image). Optionally build the TPM provider:

```bash
make cryptoauth armhf   # or arm64
```

## PKCS#11 on device

Install [`../cryptoauth/`](../cryptoauth/) configs and `libcryptoauth.so`, then:

```bash
ssh -o PKCS11Provider=/usr/lib/libcryptoauth.so -o PKCS11Enable=yes user@host
```

Or set in `~/.ssh/config`:

```text
Host *
  PKCS11Provider /usr/lib/libcryptoauth.so
  PKCS11Enable yes
```

## Notes

- Built **without PAM** to simplify cross-compilation; enable PAM later if your image requires it.
- Configure checks `ENABLE_PKCS11` in `config.h` during the build; the script fails if PKCS#11 is disabled.
