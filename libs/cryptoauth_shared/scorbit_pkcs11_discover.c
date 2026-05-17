/****************************************************************************
 * Scorbit PKCS#11 CDC TPM discovery (ported from libs/nfc/include/nfc/CdcTpm.h).
 ****************************************************************************/

#include "scorbit_pkcs11_discover.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__) || defined(__APPLE__)

#    include <dirent.h>
#    include <sys/select.h>
#    include <sys/stat.h>
#    include <termios.h>

#    define SCORBIT_PKCS11_DISCOVER_MAX_DEVICES 32
#    define SCORBIT_PKCS11_DISCOVER_PATH_LEN 64

static int serial_open(const char *path, int *fd_out)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return -1;
    }

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag = CS8 | CLOCAL | CREAD;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    *fd_out = fd;
    return 0;
}

static int serial_read(int fd, void *buf, size_t max_len, int timeout_ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &fds, NULL, NULL, &tv);
    if (ret <= 0) {
        return ret;
    }

    return (int)read(fd, buf, max_len);
}

static void serial_flush(int fd)
{
    uint8_t tmp[64];
    while (serial_read(fd, tmp, sizeof(tmp), 1) > 0) {
    }
}

static int identify_tpm_device(const char *path)
{
    int fd = -1;
    if (serial_open(path, &fd) != 0) {
        return -1;
    }

    serial_flush(fd);

    static const char cmd[] = "\nboard:device(00)\n";
    if (write(fd, cmd, sizeof(cmd) - 1u) != (ssize_t)(sizeof(cmd) - 1u)) {
        close(fd);
        return -1;
    }

    char response[256];
    memset(response, 0, sizeof(response));
    int total = 0;

    for (int attempt = 0; attempt < 5; ++attempt) {
        int n = serial_read(fd, response + total, sizeof(response) - 1u - (size_t)total, 100);
        if (n > 0) {
            total += n;
            if (strstr(response, "ECC508") != NULL) {
                break;
            }
        } else if (total > 0) {
            break;
        }
    }

    close(fd);
    return (total > 0 && strstr(response, "ECC508") != NULL) ? 0 : -1;
}

static int path_compare(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static int list_usb_devices(char devices[][SCORBIT_PKCS11_DISCOVER_PATH_LEN], int max_devices)
{
    DIR *dir = opendir("/dev");
    if (dir == NULL) {
        return 0;
    }

#    if defined(__APPLE__)
    static const char prefix[] = "cu.usbmodem";
#    else
    static const char prefix[] = "ttyACM";
#    endif

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && count < max_devices) {
        if (strncmp(entry->d_name, prefix, sizeof(prefix) - 1u) != 0) {
            continue;
        }

        char path[SCORBIT_PKCS11_DISCOVER_PATH_LEN];
        int n = snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            continue;
        }

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISCHR(st.st_mode)) {
            continue;
        }

        (void)snprintf(devices[count], SCORBIT_PKCS11_DISCOVER_PATH_LEN, "%s", path);
        ++count;
    }

    closedir(dir);

    if (count > 1) {
        qsort(devices, (size_t)count, SCORBIT_PKCS11_DISCOVER_PATH_LEN, path_compare);
    }

    return count;
}

int scorbit_pkcs11_discover_cdc(char *path_out, size_t path_out_len)
{
    if (path_out == NULL || path_out_len == 0) {
        return -1;
    }

    path_out[0] = '\0';

    char devices[SCORBIT_PKCS11_DISCOVER_MAX_DEVICES][SCORBIT_PKCS11_DISCOVER_PATH_LEN];
    const int count = list_usb_devices(devices, SCORBIT_PKCS11_DISCOVER_MAX_DEVICES);

    for (int i = 0; i < count; ++i) {
        if (identify_tpm_device(devices[i]) == 0) {
            const int n = snprintf(path_out, path_out_len, "%s", devices[i]);
            if (n < 0 || (size_t)n >= path_out_len) {
                return -1;
            }
            return 0;
        }
    }

    return -1;
}

#else

int scorbit_pkcs11_discover_cdc(char *path_out, size_t path_out_len)
{
    (void)path_out;
    (void)path_out_len;
    return -1;
}

#endif
