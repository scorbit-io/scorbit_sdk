/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "sdk_loader.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <filesystem>
#include <string>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace scorbit_godot {

namespace {

using namespace godot;

SdkApi g_api;
bool g_tried = false;
bool g_loaded = false;
std::string g_loadedPath; // UTF-8: a godot::String global would be built before Godot is ready
std::string g_error;      // why the SDK didn't load, UTF-8 as above

#ifdef _WIN32
using Handle = HMODULE;

Handle openLibrary(const String &path)
{
    const auto utf16 = path.utf16();
    // LoadLibraryEx needs a fully qualified path with backslashes; Godot's paths use '/'.
    auto native = std::filesystem::path(reinterpret_cast<const wchar_t *>(utf16.get_data()));
    native.make_preferred();
    // The SDK's own dependencies are found beside it, then in the system folders.
    return LoadLibraryExW(native.c_str(), nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

void *symbol(Handle library, const char *name)
{
    return reinterpret_cast<void *>(GetProcAddress(library, name));
}

void closeLibrary(Handle library)
{
    FreeLibrary(library);
}

String lastLoadError()
{
    return "error " + String::num_int64(static_cast<int64_t>(GetLastError()));
}

// This extension's own folder, where the SDK is shipped beside it.
String moduleDir()
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleDir), &self);
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(self, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    const auto dir = std::filesystem::path(buffer).parent_path().u8string();
    return String::utf8(reinterpret_cast<const char *>(dir.c_str()));
}
#else
using Handle = void *;

Handle openLibrary(const String &path)
{
    return dlopen(path.utf8().get_data(), RTLD_NOW | RTLD_LOCAL);
}

void *symbol(Handle library, const char *name)
{
    return dlsym(library, name);
}

void closeLibrary(Handle library)
{
    dlclose(library);
}

String lastLoadError()
{
    const char *error = dlerror();
    return error ? String::utf8(error) : String("unknown error");
}

// This extension's own folder, where the SDK is shipped beside it.
String moduleDir()
{
    Dl_info info {};
    dladdr(reinterpret_cast<void *>(&moduleDir), &info);
    return String::utf8(info.dli_fname ? info.dli_fname : "").get_base_dir();
}
#endif

struct Paths {
    String bundled; // shipped with the add-on or the exported game; read-only for all we know
    String copy;    // in the game's user folder, where the SDK's updater can replace it
    String sidecar; // SHA-256 of the bundled file the copy was made from
};

Paths paths()
{
    const String file = SCORBIT_GODOT_SDK_FILE;
    const String copyDir = OS::get_singleton()->get_user_data_dir().path_join("scorbit/sdk");
    return {moduleDir().path_join(file), copyDir.path_join(file),
            copyDir.path_join(file + String(".bundled-sha256"))};
}

bool resolve(Handle library, SdkApi &api, String &missing)
{
#define SCORBIT_GODOT_SDK_RESOLVE(name)                                                            \
    api.name = reinterpret_cast<decltype(api.name)>(symbol(library, #name));                       \
    if (!api.name) {                                                                               \
        missing = #name;                                                                           \
        return false;                                                                              \
    }
    SCORBIT_GODOT_SDK_FUNCTIONS(SCORBIT_GODOT_SDK_RESOLVE)
#undef SCORBIT_GODOT_SDK_RESOLVE
    return true;
}

// Loads @p path and resolves every function the add-on calls. Nothing is kept on failure.
bool tryLoad(const String &path, String &why)
{
    const Handle library = openLibrary(path);
    if (!library) {
        why = lastLoadError();
        return false;
    }
    SdkApi api;
    String missing;
    if (!resolve(library, api, missing)) {
        why = "it has no " + missing;
        closeLibrary(library);
        return false;
    }
    g_api = api;
    g_loadedPath = path.utf8().get_data();
    return true;
}

String readSidecar(const String &path)
{
    const auto file = FileAccess::open(path, FileAccess::READ);
    return file.is_valid() ? file->get_as_text().strip_edges() : String();
}

// Moves @p temp over @p target in one step, so readers see the old file or the new one.
bool replaceFile(const String &temp, const String &target)
{
    if (DirAccess::rename_absolute(temp, target) == OK) {
        return true;
    }
    // Where a rename can't replace a file, remove it first.
    if (DirAccess::remove_absolute(target) == OK
        && DirAccess::rename_absolute(temp, target) == OK) {
        return true;
    }
    DirAccess::remove_absolute(temp);
    return false;
}

// Replaces the copy with the bundled file, then records which bundled file it came from. Staged
// under a name of this process's own, so two instances of the game never write the same file.
// False if the user folder can't take it.
bool installCopy(const Paths &p, const String &bundledHash)
{
    const String suffix = ".new-" + String::num_int64(OS::get_singleton()->get_process_id());
    const String copyTemp = p.copy + suffix;
    const String sidecarTemp = p.sidecar + suffix;
    if (DirAccess::make_dir_recursive_absolute(p.copy.get_base_dir()) != OK
        || DirAccess::copy_absolute(p.bundled, copyTemp) != OK) {
        DirAccess::remove_absolute(copyTemp);
        return false;
    }
    if (!replaceFile(copyTemp, p.copy)) {
        return false;
    }
    {
        const auto sidecar = FileAccess::open(sidecarTemp, FileAccess::WRITE);
        if (sidecar.is_null()) {
            return false;
        }
        sidecar->store_string(bundledHash);
    } // closed before the rename
    return replaceFile(sidecarTemp, p.sidecar);
}

} // namespace

// The copy in the user folder is the one the SDK's updater replaces. A game that ships a different
// SDK, or a copy that won't load, gets the bundled one copied again.
bool loadSdk()
{
    if (g_tried) {
        return g_loaded;
    }
    g_tried = true;

    const auto fail = [](const String &message) {
        g_error = message.utf8().get_data();
        UtilityFunctions::push_error(message);
        return false;
    };

    // Only the copy in the user folder is ever run: the SDK updates the library it runs from, and
    // the one shipped with the game may sit in a read-only or signed install. If the copy can't be
    // made or loaded, Scorbit stays off and says why, so the folder can be fixed.
    const Paths p = paths();
    if (!FileAccess::file_exists(p.bundled)) {
        return fail("Scorbit can't start: its SDK library is missing from the game: " + p.bundled);
    }
    const String bundledHash = FileAccess::get_sha256(p.bundled);
    // A copy made from another bundled SDK is stale: the game shipped a different one.
    const bool copyCurrent =
            FileAccess::file_exists(p.copy) && readSidecar(p.sidecar) == bundledHash;
    const String cantCopy = "Scorbit can't start: it can't copy its SDK library into "
                          + p.copy.get_base_dir()
                          + ". Make sure that folder can be written by the user running the game.";
    if (!copyCurrent && !installCopy(p, bundledHash)) {
        return fail(cantCopy);
    }

    String why;
    if (tryLoad(p.copy, why)) {
        g_loaded = true;
        return true;
    }
    // A damaged copy is replaced from the game and tried once more.
    UtilityFunctions::push_warning("Scorbit: can't load the SDK at " + p.copy + " (" + why
                                   + "), copying it again");
    if (!installCopy(p, bundledHash)) {
        return fail(cantCopy);
    }
    if (!tryLoad(p.copy, why)) {
        return fail("Scorbit can't start: it can't load its SDK library from " + p.copy + " (" + why
                    + ").");
    }
    g_loaded = true;
    return true;
}

String sdkError()
{
    return String::utf8(g_error.c_str());
}

bool sdkLoaded()
{
    return g_loaded;
}

const SdkApi &api()
{
    return g_api;
}

Dictionary sdkPaths()
{
    const Paths p = paths();
    Dictionary result;
    result["bundled"] = p.bundled;
    result["copy"] = p.copy;
    result["sidecar"] = p.sidecar;
    result["loaded"] = String::utf8(g_loadedPath.c_str());
    return result;
}

} // namespace scorbit_godot
