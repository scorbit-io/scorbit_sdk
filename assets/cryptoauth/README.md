# PKCS#11 config for standalone `libcryptoauth.so`

Built with `make cryptoauth` (or `make cryptoauth armhf`). The library expects a main config file and slot configs under a filestore directory (paths are compile-time; check with `strings libcryptoauth.so | grep -E '/etc|/var/'`).

## On-device layout

```text
/etc/cryptoauth/cryptoauth.conf     # filestore = /var/lib/cryptoauth/
/var/lib/cryptoauth/0.conf          # slot 0: interface + device + objects
```

Copy from this directory:

- `cryptoauth.conf` -> `/etc/cryptoauth/cryptoauth.conf`
- `0.conf.cdc.example` or `0.conf.hid.example` -> `/var/lib/cryptoauth/0.conf` (edit paths/IDs as needed)

## Interface lines

**USB CDC (UART)** — matches Scorbit SDK `Tpm::tryUsbBus` (auto-discovers the probe CDC port; no hardcoded `/dev/ttyACM*`):

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

## OpenSSH

```bash
ssh -o PKCS11Provider=/usr/lib/libcryptoauth.so -o PKCS11Enable=yes user@host
```

`C_Initialize` error `5` (`CKR_GENERAL_ERROR`) usually means missing config files under the paths above, not a missing `C_GetFunctionList` symbol.

Verify symbol: `nm -D /usr/lib/libcryptoauth.so | grep C_GetFunctionList`
