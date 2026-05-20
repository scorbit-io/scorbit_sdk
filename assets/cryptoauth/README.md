# PKCS#11 config for standalone `libcryptoauth.so`

Built with `make cryptoauth` (or `make cryptoauth armhf`). The library expects a main config file and slot configs under a filestore directory. Paths are **compile-time** (baked into the `.so`).

Verify paths:

```bash
strings /usr/lib/libcryptoauth.so | grep -E '/etc|/var/|cryptoauth'
```

Expect:

```text
/usr/local/scorbit/openssh/etc/cryptoauthilb/cryptoauthlib.conf
```

(and `filestore = /usr/local/scorbit/openssh/etc/cryptoauthilb/` in that file). Rebuild if you see a relative path without a leading `/`.

## On-device layout

```text
/usr/local/scorbit/openssh/etc/cryptoauthilb/cryptoauthlib.conf
/usr/local/scorbit/openssh/etc/cryptoauthilb/0.conf
```

Copy from this directory:

- [`cryptoauthlib.conf`](cryptoauthlib.conf) → `/usr/local/scorbit/openssh/etc/cryptoauthlib/cryptoauthlib.conf`
- `0.conf.cdc.example` or `0.conf.hid.example` → `/usr/local/scorbit/openssh/etc/cryptoauthlib/0.conf`

## Interface lines

**USB CDC (UART)** — matches Scorbit SDK `Tpm::tryUsbBus` (auto-discovers the probe CDC port):

```ini
interface = uart,auto,c0,115200
```

Use `auto` or `discover`, or leave the path empty (`interface = uart,,c0,115200`). To pin a specific device for debugging:

```ini
interface = uart,/dev/ttyACM0,c0,115200
```

**USB HID (Scorbit probe)**:

```ini
interface = hid,i2c,c0,cafe,4005
```

Optional 4th/5th fields override HID VID/PID (defaults: Microchip kit `03eb:2312`).

## Troubleshooting `pkcs11_add_provider: PKCS #11 error`

1. **Paths** — `strings` vs files on disk (authoritative):

   ```bash
   strace -e openat,open -f p11tool --provider=/usr/lib/libcryptoauth.so --list-tokens 2>&1 \
     | grep -E 'conf|ENOENT|C_Initialize'
   ```

   `ENOENT` on `/usr/local/scorbit/openssh/etc/cryptoauthlib/cryptoauthlib.conf` → install [`cryptoauthlib.conf`](cryptoauthlib.conf) there.

2. **Filestore** — `/var/lib/cryptoauthlib/0.conf` must exist (not only `slot.conf.tmpl`).

3. **CDC** (for `uart,auto,...`): user in `dialout`, probe not held by another process (`lsof /dev/ttyACM*`).

4. **Dependencies**: `ldd /usr/lib/libcryptoauth.so`

## OpenSSH

```bash
ssh -o PKCS11Provider=/usr/lib/libcryptoauth.so -o PKCS11Enable=yes user@host
```

`C_Initialize` error `5` usually means missing config, CDC discovery failure, or device init failure.

Verify symbol: `nm -D /usr/lib/libcryptoauth.so | grep C_GetFunctionList`
