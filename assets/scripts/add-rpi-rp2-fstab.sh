#!/usr/bin/env bash

# Scorbit SDK
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Add or remove an /etc/fstab entry so the RP2040 UF2 volume (label RPI-RP2)
# can be mounted without root and is world-writable (vfat umask).
#
# Typical use from a .deb postinst/prerm (as root):
#   add-rpi-rp2-fstab.sh add
#   add-rpi-rp2-fstab.sh remove
#
# Notes:
# - Mounts at boot only if the device is already present (e.g. probe left plugged in).
#   With no device at boot, nofail avoids boot failure.
# - After hotplug, any user can run:  mount /mnt/RPI-RP2
#   (or mount the path you set with MOUNT_POINT=...).
# - Pure fstab cannot mount on every USB insert; for full hotplug automount use udisks
#   or a udev rule in addition to this entry.

set -euo pipefail

readonly FSTAB_PATH="${FSTAB_PATH:-/etc/fstab}"
readonly MOUNT_POINT="${MOUNT_POINT:-/mnt/RPI-RP2}"
readonly FSTAB_BEGIN="# >>> scorbit: RPI-RP2 UF2 (begin)"
readonly FSTAB_END="# <<< scorbit: RPI-RP2 UF2 (end)"

usage() {
    echo "Usage: $0 add|remove [--dry-run]" >&2
    echo "  Environment: FSTAB_PATH, MOUNT_POINT (default $MOUNT_POINT)" >&2
    exit 1
}

require_root() {
    if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
        echo "This script must run as root (e.g. from package postinst)." >&2
        exit 1
    fi
}

fstab_has_block() {
    grep -Fq "$FSTAB_BEGIN" "$FSTAB_PATH" 2>/dev/null
}

add_entry() {
    local dry_run="${1:-0}"
    local tmp
    tmp="$(mktemp)"

    if fstab_has_block; then
        echo "fstab already contains the scorbit RPI-RP2 block; nothing to do." >&2
        rm -f "$tmp"
        return 0
    fi

    if [[ ! -f "$FSTAB_PATH" ]]; then
        echo "Missing $FSTAB_PATH" >&2
        rm -f "$tmp"
        exit 1
    fi

    local backup_file="${FSTAB_PATH}.scorbit.bak.$(date +%Y%m%d%H%M%S)"
    cp -a "$FSTAB_PATH" "$backup_file"

    # nosuid,nodev: reasonable defaults for a removable FAT volume.
    # nofail: do not block boot if the probe is unplugged.
    # users: any user may mount/unmount (mount(8) uses fstab entry).
    # umask=000: new files/dirs on vfat appear world-writable (all users can write).
    local line
    line=$(
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            '/dev/disk/by-label/RPI-RP2' \
            "$MOUNT_POINT" \
            'vfat' \
            'nosuid,nodev,nofail,users,umask=000,shortname=mixed,utf8=1,flush' \
            '0' \
            '0'
    )

    {
        cat "$FSTAB_PATH"
        printf '%s\n%s\n%s\n' "$FSTAB_BEGIN" "$line" "$FSTAB_END"
    } >"$tmp"

    if [[ "$dry_run" -eq 1 ]]; then
        echo "Would append to $FSTAB_PATH:" >&2
        printf '%s\n%s\n%s\n' "$FSTAB_BEGIN" "$line" "$FSTAB_END" >&2
        rm -f "$tmp"
        return 0
    fi

    install -m 644 "$tmp" "$FSTAB_PATH"
    rm -f "$tmp"

    if ! fstab_has_block; then
        echo "ERROR: $FSTAB_PATH was updated but the expected scorbit block is missing; restoring backup." >&2
        if cp -a "$backup_file" "$FSTAB_PATH"; then
            rm -f "$backup_file"
            echo "Restored $FSTAB_PATH from backup; nothing changed." >&2
        else
            echo "ERROR: restore failed. Recover manually with: cp -a '$backup_file' '$FSTAB_PATH'" >&2
            exit 1
        fi
        exit 1
    fi

    rm -f "$backup_file"
    echo "Updated $FSTAB_PATH." >&2

    install -d -m 0755 "$MOUNT_POINT"
    echo "Created mount point $MOUNT_POINT" >&2

    if command -v systemctl >/dev/null 2>&1; then
        systemctl daemon-reload
    fi

    if [[ -e /dev/disk/by-label/RPI-RP2 ]]; then
        if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
            echo "$MOUNT_POINT is already mounted." >&2
        else
            mount "$MOUNT_POINT" && echo "Mounted $MOUNT_POINT" >&2 || true
        fi
    fi
}

remove_entry() {
    local dry_run="${1:-0}"
    local tmp
    tmp="$(mktemp)"

    if ! fstab_has_block; then
        echo "No scorbit RPI-RP2 block in $FSTAB_PATH; nothing to remove." >&2
        rm -f "$tmp"
        return 0
    fi

    if [[ "$dry_run" -eq 1 ]]; then
        echo "Would remove scorbit RPI-RP2 block from $FSTAB_PATH" >&2
        rm -f "$tmp"
        return 0
    fi

    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT" || true
    fi

    awk -v begin="$FSTAB_BEGIN" -v end="$FSTAB_END" '
        $0 == begin { skip=1; next }
        $0 == end { skip=0; next }
        !skip { print }
    ' "$FSTAB_PATH" >"$tmp"
    install -m 644 "$tmp" "$FSTAB_PATH"
    rm -f "$tmp"
    echo "Removed scorbit RPI-RP2 block from $FSTAB_PATH" >&2
}

main() {
    local cmd="${1:-}"
    local dry=0
    shift || true
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --dry-run) dry=1 ;;
            *) usage ;;
        esac
        shift
    done

    case "$cmd" in
        add)
            require_root
            add_entry "$dry"
            ;;
        remove)
            require_root
            remove_entry "$dry"
            ;;
        -h|--help|'')
            usage
            ;;
        *)
            usage
            ;;
    esac
}

main "$@"
