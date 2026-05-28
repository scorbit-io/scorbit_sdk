/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#include "wpa_supplicant_dbus.h"
#include <nlohmann/json.hpp>
#if defined(__linux__)
#include <dlfcn.h>
#endif
#include <cstdint>

namespace scorbit {
namespace detail {
namespace wifi {
namespace {

#if defined(__linux__)
constexpr auto WPA_INTERFACE = "fi.w1.wpa_supplicant1.Interface";
constexpr int DBUS_BUS_SYSTEM = 1;
constexpr int DBUS_MESSAGE_TYPE_SIGNAL = 4;
constexpr int DBUS_TYPE_ARRAY = 'a';
constexpr int DBUS_TYPE_BOOLEAN = 'b';
constexpr int DBUS_TYPE_DICT_ENTRY = 'e';
constexpr int DBUS_TYPE_OBJECT_PATH = 'o';
constexpr int DBUS_TYPE_STRING = 's';
constexpr int DBUS_TYPE_VARIANT = 'v';

struct DBusConnection;
struct DBusMessage;
using dbus_bool_t = std::uint32_t;

// Public ABI shape from libdbus-1. We only pass it to libdbus functions.
struct DBusMessageIter {
    void *dummy1;
    void *dummy2;
    std::uint32_t dummy3;
    int dummy4;
    int dummy5;
    int dummy6;
    int dummy7;
    int dummy8;
    int dummy9;
    int dummy10;
    int dummy11;
    int pad1;
    int pad2;
    void *pad3;
};

struct DbusApi {
    void *handle {nullptr};

    DBusConnection *(*busGet)(int, void *) {nullptr};
    void (*busAddMatch)(DBusConnection *, const char *, void *) {nullptr};
    void (*busRemoveMatch)(DBusConnection *, const char *, void *) {nullptr};
    void (*connectionFlush)(DBusConnection *) {nullptr};
    dbus_bool_t (*connectionReadWrite)(DBusConnection *, int) {nullptr};
    DBusMessage *(*connectionPopMessage)(DBusConnection *) {nullptr};
    void (*connectionUnref)(DBusConnection *) {nullptr};
    int (*messageGetType)(DBusMessage *) {nullptr};
    const char *(*messageGetMember)(DBusMessage *) {nullptr};
    const char *(*messageGetPath)(DBusMessage *) {nullptr};
    void (*messageUnref)(DBusMessage *) {nullptr};
    dbus_bool_t (*messageIterInit)(DBusMessage *, DBusMessageIter *) {nullptr};
    int (*messageIterGetArgType)(DBusMessageIter *) {nullptr};
    void (*messageIterGetBasic)(DBusMessageIter *, void *) {nullptr};
    dbus_bool_t (*messageIterNext)(DBusMessageIter *) {nullptr};
    void (*messageIterRecurse)(DBusMessageIter *, DBusMessageIter *) {nullptr};

    DbusApi() = default;
    DbusApi(const DbusApi &) = delete;
    DbusApi &operator=(const DbusApi &) = delete;

    DbusApi(DbusApi &&other) noexcept
        : handle(other.handle)
        , busGet(other.busGet)
        , busAddMatch(other.busAddMatch)
        , busRemoveMatch(other.busRemoveMatch)
        , connectionFlush(other.connectionFlush)
        , connectionReadWrite(other.connectionReadWrite)
        , connectionPopMessage(other.connectionPopMessage)
        , connectionUnref(other.connectionUnref)
        , messageGetType(other.messageGetType)
        , messageGetMember(other.messageGetMember)
        , messageGetPath(other.messageGetPath)
        , messageUnref(other.messageUnref)
        , messageIterInit(other.messageIterInit)
        , messageIterGetArgType(other.messageIterGetArgType)
        , messageIterGetBasic(other.messageIterGetBasic)
        , messageIterNext(other.messageIterNext)
        , messageIterRecurse(other.messageIterRecurse)
    {
        other.handle = nullptr;
    }

    DbusApi &operator=(DbusApi &&other) noexcept
    {
        if (this == &other) {
            return *this;
        }
        if (handle != nullptr) {
            dlclose(handle);
        }
        handle = other.handle;
        busGet = other.busGet;
        busAddMatch = other.busAddMatch;
        busRemoveMatch = other.busRemoveMatch;
        connectionFlush = other.connectionFlush;
        connectionReadWrite = other.connectionReadWrite;
        connectionPopMessage = other.connectionPopMessage;
        connectionUnref = other.connectionUnref;
        messageGetType = other.messageGetType;
        messageGetMember = other.messageGetMember;
        messageGetPath = other.messageGetPath;
        messageUnref = other.messageUnref;
        messageIterInit = other.messageIterInit;
        messageIterGetArgType = other.messageIterGetArgType;
        messageIterGetBasic = other.messageIterGetBasic;
        messageIterNext = other.messageIterNext;
        messageIterRecurse = other.messageIterRecurse;
        other.handle = nullptr;
        return *this;
    }

    ~DbusApi()
    {
        if (handle != nullptr) {
            dlclose(handle);
        }
    }

    explicit operator bool() const
    {
        return handle != nullptr && busGet != nullptr && busAddMatch != nullptr
            && busRemoveMatch != nullptr && connectionFlush != nullptr
            && connectionReadWrite != nullptr && connectionPopMessage != nullptr
            && connectionUnref != nullptr && messageGetType != nullptr
            && messageGetMember != nullptr && messageGetPath != nullptr && messageUnref != nullptr
            && messageIterInit != nullptr && messageIterGetArgType != nullptr
            && messageIterGetBasic != nullptr && messageIterNext != nullptr
            && messageIterRecurse != nullptr;
    }
};

template<typename T>
T loadSymbol(void *handle, const char *name)
{
    return reinterpret_cast<T>(dlsym(handle, name));
}

DbusApi loadDbusApi()
{
    DbusApi api;
    for (const auto *name : {"libdbus-1.so.3", "libdbus-1.so"}) {
        api.handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (api.handle != nullptr) {
            break;
        }
    }

    if (api.handle == nullptr) {
        return api;
    }

    api.busGet = loadSymbol<decltype(api.busGet)>(api.handle, "dbus_bus_get");
    api.busAddMatch = loadSymbol<decltype(api.busAddMatch)>(api.handle, "dbus_bus_add_match");
    api.busRemoveMatch =
            loadSymbol<decltype(api.busRemoveMatch)>(api.handle, "dbus_bus_remove_match");
    api.connectionFlush =
            loadSymbol<decltype(api.connectionFlush)>(api.handle, "dbus_connection_flush");
    api.connectionReadWrite =
            loadSymbol<decltype(api.connectionReadWrite)>(api.handle, "dbus_connection_read_write");
    api.connectionPopMessage =
            loadSymbol<decltype(api.connectionPopMessage)>(api.handle, "dbus_connection_pop_message");
    api.connectionUnref =
            loadSymbol<decltype(api.connectionUnref)>(api.handle, "dbus_connection_unref");
    api.messageGetType = loadSymbol<decltype(api.messageGetType)>(api.handle, "dbus_message_get_type");
    api.messageGetMember =
            loadSymbol<decltype(api.messageGetMember)>(api.handle, "dbus_message_get_member");
    api.messageGetPath = loadSymbol<decltype(api.messageGetPath)>(api.handle, "dbus_message_get_path");
    api.messageUnref = loadSymbol<decltype(api.messageUnref)>(api.handle, "dbus_message_unref");
    api.messageIterInit =
            loadSymbol<decltype(api.messageIterInit)>(api.handle, "dbus_message_iter_init");
    api.messageIterGetArgType = loadSymbol<decltype(api.messageIterGetArgType)>(
            api.handle, "dbus_message_iter_get_arg_type");
    api.messageIterGetBasic = loadSymbol<decltype(api.messageIterGetBasic)>(
            api.handle, "dbus_message_iter_get_basic");
    api.messageIterNext =
            loadSymbol<decltype(api.messageIterNext)>(api.handle, "dbus_message_iter_next");
    api.messageIterRecurse =
            loadSymbol<decltype(api.messageIterRecurse)>(api.handle, "dbus_message_iter_recurse");

    return api;
}

std::string readString(const DbusApi &api, DBusMessageIter *iter)
{
    if (api.messageIterGetArgType(iter) != DBUS_TYPE_STRING
        && api.messageIterGetArgType(iter) != DBUS_TYPE_OBJECT_PATH) {
        return {};
    }

    const char *value = nullptr;
    api.messageIterGetBasic(iter, &value);
    return value == nullptr ? std::string {} : std::string {value};
}

bool readBool(const DbusApi &api, DBusMessageIter *iter)
{
    if (api.messageIterGetArgType(iter) != DBUS_TYPE_BOOLEAN) {
        return false;
    }

    dbus_bool_t value = false;
    api.messageIterGetBasic(iter, &value);
    return value != 0;
}

std::string stateEventKind(const std::string &state)
{
    if (state == "completed") {
        return "assoc";
    }
    if (state == "disconnected" || state == "inactive" || state == "interface_disabled") {
        return "deauth";
    }
    if (state == "scanning") {
        return "scan";
    }
    return {};
}

std::optional<std::string> readPropertiesChangedState(const DbusApi &api, DBusMessage *message)
{
    DBusMessageIter iter;
    if (!api.messageIterInit(message, &iter)) {
        return std::nullopt;
    }

    const auto changedInterface = readString(api, &iter);
    if (changedInterface != WPA_INTERFACE) {
        return std::nullopt;
    }

    if (!api.messageIterNext(&iter) || api.messageIterGetArgType(&iter) != DBUS_TYPE_ARRAY) {
        return std::nullopt;
    }

    DBusMessageIter dict;
    api.messageIterRecurse(&iter, &dict);
    while (api.messageIterGetArgType(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        api.messageIterRecurse(&dict, &entry);
        const auto key = readString(api, &entry);
        if (key == "State" && api.messageIterNext(&entry)
            && api.messageIterGetArgType(&entry) == DBUS_TYPE_VARIANT) {
            DBusMessageIter variant;
            api.messageIterRecurse(&entry, &variant);
            return readString(api, &variant);
        }
        api.messageIterNext(&dict);
    }

    return std::nullopt;
}

std::optional<bool> readScanDoneSuccess(const DbusApi &api, DBusMessage *message)
{
    DBusMessageIter iter;
    if (!api.messageIterInit(message, &iter)) {
        return std::nullopt;
    }

    if (api.messageIterGetArgType(&iter) == DBUS_TYPE_BOOLEAN) {
        return readBool(api, &iter);
    }

    return std::nullopt;
}

Event eventFromMessage(const DbusApi &api, DBusMessage *message)
{
    Event event;
    const auto *member = api.messageGetMember(message);
    const auto *path = api.messageGetPath(message);
    const auto memberName = member == nullptr ? std::string {} : std::string {member};

    nlohmann::json payload {
            {"source", "wpa_supplicant_dbus"},
            {"member", memberName},
            {"path", path == nullptr ? std::string {} : std::string {path}},
    };

    if (memberName == "PropertiesChanged") {
        if (const auto state = readPropertiesChangedState(api, message); state) {
            event.kind = stateEventKind(*state);
            payload["state"] = *state;
        }
    } else if (memberName == "ScanDone") {
        event.kind = "scan";
        if (const auto success = readScanDoneSuccess(api, message); success) {
            payload["success"] = *success;
        }
    } else if (memberName == "NetworkSelected" || memberName == "NetworkConnected") {
        event.kind = "assoc";
    } else if (memberName == "NetworkDisconnected" || memberName == "Disconnected") {
        event.kind = "deauth";
    }

    event.payloadJson = payload.dump();
    return event;
}
#endif

} // namespace

WpaSupplicantDbusListener::WpaSupplicantDbusListener(EventCallback callback)
    : m_callback(std::move(callback))
{
}

WpaSupplicantDbusListener::~WpaSupplicantDbusListener()
{
    stop();
}

bool WpaSupplicantDbusListener::start()
{
#if defined(__linux__)
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) {
        return true;
    }

    m_thread = std::thread {[this] { run(); }};
    return true;
#else
    return false;
#endif
}

void WpaSupplicantDbusListener::stop()
{
    if (!m_running.exchange(false)) {
        return;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool WpaSupplicantDbusListener::isRunning() const
{
    return m_running;
}

void WpaSupplicantDbusListener::run()
{
#if defined(__linux__)
    auto api = loadDbusApi();
    if (!api) {
        m_running = false;
        return;
    }

    DBusConnection *connection = api.busGet(DBUS_BUS_SYSTEM, nullptr);
    if (connection == nullptr) {
        m_running = false;
        return;
    }

    const char *matches[] = {
            "type='signal',sender='fi.w1.wpa_supplicant1',"
            "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
            "type='signal',sender='fi.w1.wpa_supplicant1',"
            "interface='fi.w1.wpa_supplicant1.Interface'",
    };

    for (const auto *match : matches) {
        api.busAddMatch(connection, match, nullptr);
        api.connectionFlush(connection);
    }

    while (m_running) {
        api.connectionReadWrite(connection, 500);
        DBusMessage *message = api.connectionPopMessage(connection);
        if (message == nullptr) {
            continue;
        }

        if (api.messageGetType(message) == DBUS_MESSAGE_TYPE_SIGNAL) {
            auto event = eventFromMessage(api, message);
            if (!event.kind.empty()) {
                emit(std::move(event));
            }
        }

        api.messageUnref(message);
    }

    for (const auto *match : matches) {
        api.busRemoveMatch(connection, match, nullptr);
        api.connectionFlush(connection);
    }
    api.connectionUnref(connection);
#endif
}

void WpaSupplicantDbusListener::emit(Event event)
{
    std::scoped_lock lock(m_callbackMutex);
    if (m_callback) {
        m_callback(std::move(event));
    }
}

} // namespace wifi
} // namespace detail
} // namespace scorbit
