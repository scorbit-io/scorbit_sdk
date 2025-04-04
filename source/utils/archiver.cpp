/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Apr 2025
 *
 ****************************************************************************/

#include "archiver.h"
#include "../logger.h"

#include <boost/filesystem.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <memory>

namespace {

constexpr auto READ_BLOCK_SIZE = 10240;

namespace fs = boost::filesystem;

struct ArchiveDeleter {
    void operator()(archive *a) const
    {
        if (a) {
            archive_read_close(a); // safe even if not opened
            archive_read_free(a);
        }
    }
};

struct ArchiveWriterDeleter {
    void operator()(archive *a) const
    {
        if (a) {
            archive_write_close(a); // safe even if not opened
            archive_write_free(a);
        }
    }
};

using ArchiveReaderPtr = std::unique_ptr<archive, ArchiveDeleter>;
using ArchiveWriterPtr = std::unique_ptr<archive, ArchiveWriterDeleter>;

int copy_data(struct archive *ar, struct archive *aw)
{
    const void *buff;
    size_t size;
    la_int64_t offset;

    while (true) {
        auto r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return ARCHIVE_OK;
        if (r < ARCHIVE_OK)
            return r;
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK)
            return r;
    }
}

} // namespace

namespace scorbit {
namespace detail {

bool extract(const std::string &archivePath, const std::string &outputDir)
{
    try {
        if (!fs::exists(outputDir)) {
            fs::create_directories(outputDir);
        }
    } catch (const fs::filesystem_error &e) {
        ERR("Failed to create output directory '{}': {}", outputDir, e.what());
        return false;
    }

    ArchiveReaderPtr reader(archive_read_new());
    ArchiveWriterPtr writer(archive_write_disk_new());

    if (!reader || !writer) {
        ERR("Failed to allocate libarchive objects");
        return false;
    }

    archive_read_support_filter_all(reader.get());
    archive_read_support_format_all(reader.get());

    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL
              | ARCHIVE_EXTRACT_FFLAGS;

    archive_write_disk_set_options(writer.get(), flags);
    archive_write_disk_set_standard_lookup(writer.get());

    if (archive_read_open_filename(reader.get(), archivePath.c_str(), READ_BLOCK_SIZE)
        != ARCHIVE_OK) {
        ERR("Failed to open archive '{}': {}", archivePath, archive_error_string(reader.get()));
        return false;
    }

    struct archive_entry *entry = nullptr;
    int r = ARCHIVE_OK;

    while ((r = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        // Prepend outputDir to the pathname
        const char *entryPath = archive_entry_pathname(entry);
        fs::path fullPath = fs::path(outputDir) / entryPath;
        archive_entry_set_pathname(entry, fullPath.string().c_str());

        // Write header and content
        r = archive_write_header(writer.get(), entry);
        if (r < ARCHIVE_OK) {
            ERR("Failed to write header: {}", archive_error_string(writer.get()));
        } else if (archive_entry_size(entry) > 0) {
            r = copy_data(reader.get(), writer.get());
            if (r < ARCHIVE_OK) {
                ERR("Data copy error: {}", archive_error_string(writer.get()));
            }
        }

        r = archive_write_finish_entry(writer.get());
        if (r < ARCHIVE_OK) {
            ERR("Failed to finish entry: {}", archive_error_string(writer.get()));
            break;
        }
    }

    if (r != ARCHIVE_EOF) {
        ERR("Archive read error: {}", archive_error_string(reader.get()));
    }

    return r == ARCHIVE_EOF;
}

} // namespace detail
} // namespace scorbits
