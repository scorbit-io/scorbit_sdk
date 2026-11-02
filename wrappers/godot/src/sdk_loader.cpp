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

#ifdef _WIN32
using Handle = HMODULE;

Handle openLibrary(const String &path)
{
    const auto wide = path.utf16();
    // Altered search path: the SDK's own dependencies are found beside it, not beside the game.
    return LoadLibraryExW(reinterpret_cast<const wchar_t *>(wide.get_data()), nullptr,
                          LOAD_WITH_ALTERED_SEARCH_PATH);
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

// Replaces the copy with the bundled file: a temporary file renamed over it, so the copy is never
// half written. False if the user folder can't take it.
bool installCopy(const Paths &p, const String &bundledHash)
{
    const String temp = p.copy + String(".new");
    if (DirAccess::make_dir_recursive_absolute(p.copy.get_base_dir()) != OK
        || DirAccess::copy_absolute(p.bundled, temp) != OK) {
        return false;
    }
    if (DirAccess::rename_absolute(temp, p.copy) != OK) {
        // Where a rename can't replace a file, remove it first.
        if (DirAccess::remove_absolute(p.copy) != OK
            || DirAccess::rename_absolute(temp, p.copy) != OK) {
            DirAccess::remove_absolute(temp);
            return false;
        }
    }
    const auto sidecar = FileAccess::open(p.sidecar, FileAccess::WRITE);
    if (sidecar.is_null()) {
        return false;
    }
    sidecar->store_string(bundledHash);
    return true;
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

    const Paths p = paths();
    if (!FileAccess::file_exists(p.bundled)) {
        UtilityFunctions::push_error("Scorbit: the SDK library is missing: " + p.bundled);
        return false;
    }
    const String bundledHash = FileAccess::get_sha256(p.bundled);
    const bool copyCurrent =
            FileAccess::file_exists(p.copy) && readSidecar(p.sidecar) == bundledHash;
    if (!copyCurrent && !installCopy(p, bundledHash)) {
        UtilityFunctions::push_warning("Scorbit: can't copy the SDK to " + p.copy
                                       + ", so it can't update itself");
    }

    String why;
    if (FileAccess::file_exists(p.copy)) {
        if (tryLoad(p.copy, why)) {
            g_loaded = true;
            return true;
        }
        UtilityFunctions::push_warning("Scorbit: can't load the SDK at " + p.copy + " (" + why
                                       + "), using the one shipped with the game");
        installCopy(p, bundledHash); // a working copy for the next run
    }
    g_loaded = tryLoad(p.bundled, why);
    if (!g_loaded) {
        UtilityFunctions::push_error("Scorbit: can't load the SDK at " + p.bundled + ": " + why);
    }
    return g_loaded;
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
