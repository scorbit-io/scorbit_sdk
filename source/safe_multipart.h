/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * May 2025
 *
 ****************************************************************************/

#pragma once

#include <cpr/multipart.h>
#include <vector>
#include <memory>

namespace scorbit {
namespace detail {

/**
 * @brief The SafeMultipart class holds a cpr::Multipart object along with associated data buffers
 * to ensure that the data referenced by cpr::Buffer— a non-owning structure—remains valid even
 * after the original data it pointed to has been destroyed.
 */
class SafeMultipart
{
    struct Data {
        Data(cpr::Multipart &&multipart)
            : multipart(std::move(multipart))
        {
        }
        cpr::Multipart multipart;
        std::vector<std::vector<char>> buffers;
    };

public:
    explicit SafeMultipart(cpr::Multipart &&multipart)
        : m_data(std::make_shared<Data>(std::move(multipart)))
    {
        for (auto &part : m_data->multipart.parts) {
            if (part.is_buffer && part.data && part.datalen > 0) {
                m_data->buffers.emplace_back(
                        std::vector<char> {part.data, part.data + part.datalen});
                part.data = m_data->buffers.back().data();
            }
        }
    }

    cpr::Multipart &get() { return m_data->multipart; }
    const cpr::Multipart &get() const { return m_data->multipart; }

private:
    std::shared_ptr<Data> m_data;
};

} // namespace detail
} // namespace scorbit
