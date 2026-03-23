#include <centrifugo/error.h>

namespace centrifugo {

class CentrifugoErrorCategory : public std::error_category
{
public:
    auto name() const noexcept -> char const * override { return "centrifugo"; }

    auto message(int) const -> std::string override { return "message() not implemented"; }

    auto default_error_condition(int ev) const noexcept -> std::error_condition override
    {
        switch (static_cast<ErrorType>(ev)) {
        case ErrorType::PermissionDenied:
            return std::errc::permission_denied;
        case ErrorType::NotConnected:
            return std::errc::not_connected;
        default:
            return std::errc::operation_not_supported;
        }
    }
};

auto centrifugo_category() -> CentrifugoErrorCategory const &
{
    static CentrifugoErrorCategory const instance {};
    return instance;
}

auto make_error_code(ErrorType e) -> std::error_code
{
    return {static_cast<int>(e), centrifugo_category()};
}

}
