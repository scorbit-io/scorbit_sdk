# Tap pad lock (`cryptoauthlib-tappad-lock.patch`)

A tap pad's secure element (ATECC508A/608) can be used by more than one process
on the same host. cryptoauthlib idles the chip after every command, and a single
`calib_*` call such as `calib_sign` or `calib_verify_extern` issues several
commands that rely on TempKey surviving between them. If another process's
command lands in the middle, the sequence fails or uses the wrong state. This
patch adds one host-wide lock that every such process takes.

## Applying it

Apply to cryptoauthlib **3.7.9**, in this order:

1. `cryptoauthlib-build-fixes.patch`
2. `cryptoauthlib-libusb-cdc.patch`
3. `cryptoauthlib-tappad-lock.patch`

It must come after `cryptoauthlib-libusb-cdc.patch` because its hunks in
`lib/CMakeLists.txt` and `lib/hal/hal_linux_uart_userspace.c` use lines that
patch adds as context. Its new files, `lib/hal/hal_tappad_lock.{c,h}`, are
compiled wherever `hal_linux_uart_userspace.c` is (the kit-UART HAL on Linux and
macOS), and the `cryptoauth` target then carries the public compile definition
`ATCA_HAL_TAPPAD_LOCK`. Build the library position-independent if it ends up in
a shared object that may be `dlopen`ed: the lock uses `__thread`.

## Lock files

The candidates are `/var/lock/scorbit-tappad.lock`,
`/run/lock/scorbit-tappad.lock` and `/tmp/scorbit-tappad.lock`, in that fixed
order. The lock is per host, not per tty.

**A hold locks every candidate that is usable, not just the first.** Processes
can differ in which paths they can use. A directory may not exist yet early in
boot, permissions may differ between users, or a crash may leave a stale file
on one path. Under "first usable path", two such processes could each pick a
different file and both believe they hold the lock. When each holds every path
it can use, any two processes that can both use some common path exclude each
other through it. One that can use more paths simply holds more of them.

- The files are `flock()`ed one after another in the fixed candidate order,
  under one deadline, so two holders cannot deadlock each other. If any of them
  cannot be had in time, those already taken are released in reverse order and
  the call returns `ATCA_TAPPAD_BUSY`.
- Candidates that name the same file are deduplicated by device and inode, for
  example `/var/lock` as a symlink to `/run/lock`. The file is opened and locked
  once. `flock()` belongs to the open file description, so locking it through a
  second descriptor would deadlock the process against itself.
- On the outermost release every file is unlocked, last candidate first.

These directories are world-writable, so the file is opened defensively:

- It is created with `O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK`, mode 0666. Only a
  file created that way is `fchmod`ed to 0666, at once, past the umask, so
  processes running as different users share it.
- An existing file is opened with `O_NOFOLLOW|O_NONBLOCK` and no `O_CREAT`,
  read-write, or read-only if that is refused (enough for `flock()`). Its mode is
  never changed. `O_NONBLOCK` means a FIFO planted at the path cannot hang the
  open. If the file vanishes between the two opens, the create is tried once more.
- After opening, the file must be a regular file with exactly one link and mode
  0666. A symlink, a FIFO, a directory, a hard link or a file of any other mode
  at a path makes that path unusable; the others are still used. The mode must be
  exactly 0666: 0777, setuid, setgid and sticky bits are refused too.
- The mode check makes every process accept and refuse the same files whatever
  its uid. Otherwise root would lock a 0600 file that others cannot open, and
  the lock would split. If a crash left a created file at another mode (between
  the create and the `fchmod`), everyone skips that path alike and uses the
  others. That path then stays unusable until the file is removed. `/run/lock` and
  `/var/lock` are cleared at boot; the `/tmp` fallback may not be (it is not on
  hosts where `/tmp` is not a tmpfs), so a stale file there needs removing by
  hand.

Each outermost acquire re-checks the files:

- A file that was unlinked or replaced is reopened. The check is made again after
  all the `flock()`s succeed, since a lock on an orphaned inode excludes nobody;
  on a mismatch everything is released and the acquire retries within its
  deadline.
- A path that has become usable since the last hold (for example `/run/lock`
  once it exists after boot) is opened and locked from then on.

A path whose `flock()` fails with something other than `EWOULDBLOCK`/`EINTR`
(for example `ENOLCK`) is dropped for that hold. If no path can be used, the
lock degrades to in-process exclusion only, and `hal_tappad_lock_path()` (the
first file held) returns `NULL` to say so. `hal_tappad_lock_file_count()` says
how many distinct files a hold locks. Chip I/O is never failed because the lock
files are unavailable.

Tests keep off the real file with `hal_tappad_lock_set_paths()`. Where they can
only reach cryptoauthlib through a library that hides that symbol, a build
compiled with `ATCA_TAPPAD_LOCK_ENV_OVERRIDE` takes the path from the
`ATCA_TAPPAD_LOCK_PATH` environment variable instead. In this repository only
the `tests/` build defines it (via `SCORBIT_TAPPAD_LOCK_TEST_ENV`); release
builds must not.

## API (`hal/hal_tappad_lock.h`)

```c
ATCA_STATUS hal_tappad_lock(uint32_t timeout_ms);
ATCA_STATUS hal_tappad_lock_sequence(uint32_t timeout_ms); /* caller-level holds */
void        hal_tappad_unlock(void);
int         hal_tappad_lock_depth(void);
const char *hal_tappad_lock_path(void);       /* first file held */
size_t      hal_tappad_lock_file_count(void); /* distinct files held */
ATCA_STATUS hal_tappad_lock_set_paths(const char *const *paths, size_t count); /* tests */
```

- **Nesting is per thread.** A thread that holds the lock can take it again;
  only its outermost release drops the file lock. Other threads in the same
  process are excluded by an in-process mutex (`flock()` alone would not exclude
  them, since they share one open file description). A hold must be released on
  the thread that took it.
- **Bounded wait.** `timeout_ms` bounds the whole wait, in-process mutex and
  file lock together. The HAL's own holds use 2000 ms.
- **fork() safe.** A child discards inherited state (including a hold its
  parent had) and opens its own file description on first use; the fd is
  `O_CLOEXEC`. Only `fork()` is supported, not a raw `clone()` that skips the
  fork handlers.

### Fairness

Both waits poll: threads of one process `pthread_mutex_trylock()` the
in-process mutex, and processes `flock(LOCK_NB)` the file. Both back off from 1
to 8 ms plus up to 1 ms of jitter, so a waiter polls at least every ~9 ms. All
timing uses `CLOCK_MONOTONIC`, so a clock step at boot (no RTC, then NTP)
cannot stretch or cut a wait. `pthread_mutex_timedlock()` is not used: it needs
a `CLOCK_REALTIME` deadline, and its waiters are not queued in order anyway.

A caller-level hold taken with `hal_tappad_lock_sequence()` is marked as a
sequence. After releasing a sequence hold, a process waits until 20 ms have
passed (twice a waiter's longest poll interval) before it takes the file lock
again. This gives a process waiting for the lock a window in which its poll
should find it free. A process that takes the lock back to back, for example
consecutive `calib_*` calls, should therefore not starve another; in practice
the waiter gets in after about one of the holder's sequences. This is a
best-effort bound, not a strict queue, and scheduling delay can stretch it.
The gap never extends a wait past the caller's timeout. It is paid even when no
one else is waiting, so a single process spends up to 20 ms between back-to-back
sequences. Skipping it when this process saw no contention would let the holder,
which always wins at once, starve the waiter that only it could have seen.

The HAL's own per-transfer holds are plain `hal_tappad_lock()` holds and are
never spaced out, because doing so would slow every byte read. Callers that rely
on those holds alone get no fairness. They also get no atomicity (see below).

### Busy status

On timeout `hal_tappad_lock()` returns `ATCA_TAPPAD_BUSY`, defined as
`ATCA_TX_TIMEOUT` (-22). `ATCA_TX_TIMEOUT` is not returned anywhere in
cryptoauthlib 3.7.9, so it means only "another holder kept the lock". It is
transient: retry later. It is not a chip, bus or no-device status.

## What callers must do

The HAL holds the lock around each transfer it makes (`hal_uart_init`,
`hal_uart_send`, `hal_uart_receive`, `hal_uart_control`), for the native tty and
the libusb CDC transport alike. On kit-UART a receive transfer is a **single
byte** (`kit_phy_receive` reads byte by byte), so the HAL-level hold protects
only individual writes and byte reads. It does **not** keep a response together,
let alone a command or a sequence.

**Callers MUST hold the lock across each `calib_*` / `atcab_*` call, across any
multi-command sequence, and across `atcab_init_ext()`, which writes a probe to
the device.** Take these holds with `hal_tappad_lock_sequence()`. Take it before the sequence and release it after. Do not
hold it across retry back-off sleeps, so other processes can get in. Any other
code that writes to the tap pad's ttys (for example a discovery scan) must hold
it too.

Without an outer hold, a `ATCA_TAPPAD_BUSY` from one of the HAL's own holds
inside `kit_receive` is reported upward as `ATCA_GEN_FAIL`, because the kit layer
maps receive errors. Callers that hold the lock themselves never see this,
because the HAL's holds nest inside theirs and cannot time out.

### The libusb CDC transport

`hal_cdc_libusb_open` claims the device's USB interfaces, and they stay claimed
until the interface is released (`atcab_release_ext`). While one process has the
pad open this way, another process's open fails (`ATCA_COMM_FAIL`) whether or
not it holds the lock. With this transport, processes can take turns but cannot
keep the pad open side by side. To share it, a process must open it, run its
sequence and release it all inside one hold. The lock serialises the sequences;
it does not make the interface shareable. The native tty (`cdc-acm`) path has no
such restriction.

Because the second process's open fails with `ATCA_COMM_FAIL`, a pad held this
way looks gone to it, not busy. cryptoauthlib reduces the HAL's init status to
`ATCA_BAD_PARAM` (`atinit`) and then `ATCA_GEN_FAIL` (`atcab_init_ext`), so a
busy claim cannot reach the caller as `ATCA_TAPPAD_BUSY` without patching that
core init path. This patch does not.

## Behaviour when busy (this SDK)

- A chip call that cannot get the lock within its budget (2 s per `atcaRetry`
  call, counting only time spent waiting for the lock) fails with
  `ATCA_TAPPAD_BUSY`, logged once per call as "tap pad busy".
- A busy tap pad is never treated as a missing chip. The cached device is kept,
  no rediscovery runs (it would write probes into the holder's sequence), and
  the operation fails or is retried later.
- TPM discovery does not scan unless it holds the lock; if it cannot get the
  lock within 10 s, it finds nothing and reports the pad busy.
- **At authentication, a busy tap pad defers authentication indefinitely and
  never falls through to a weaker key.** Falling through would provision a soft
  key and change the device's identity for good. This applies both when the key
  is resolved and when the authentication request is signed. Authentication is
  retried with the usual backoff, and is never reported as
  `AuthenticationFailed` for this reason. The host sees `Authenticating` during
  each attempt and `NotAuthenticated` between attempts. Once deferrals have
  lasted 5 minutes, one ERR is logged ("TPM tap pad busy for over 5 min;
  authentication deferred"). A successful authentication resets this.
- A busy tap pad while signing a token refresh keeps the device authenticated
  and retries the refresh with the backoff. If the token expires first, the
  normal token-expiry handling applies.
- While authentication is deferred, host requests that need authentication wait
  on the authentication gate for up to its timeout (2 minutes) each, and then
  fail with `NotPaired`. Calls such as `startGame` and `submitGameData` are not
  refused up front. This is accepted: a long busy deferral is rare, and changing
  the gate would change every request.
- An I2C TPM is not on the tap pad and never takes the lock.
