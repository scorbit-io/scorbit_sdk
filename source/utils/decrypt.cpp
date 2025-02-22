/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Feb 2025
 *
 ****************************************************************************/

#include "bytearray.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <vector>
#include <cstring>
#include <openssl/buffer.h>
#include <openssl/kdf.h>


namespace scorbit {
namespace detail {

constexpr int AES_KEY_SIZE = 32; // AES-256
constexpr int AES_IV_SIZE = 12;
constexpr int TAG_SIZE = 16;
constexpr int PBKDF2_ITERATIONS = 10000;

std::vector<uint8_t> base64Decode(const std::string &encoded)
{
    BIO *bio, *b64;
    std::vector<uint8_t> buffer(encoded.size());

    bio = BIO_new_mem_buf(encoded.data(), encoded.size());
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // No newline handling
    int length = BIO_read(bio, buffer.data(), buffer.size());
    buffer.resize(length); // Adjust size to actual decoded length
    BIO_free_all(bio);

    return buffer;
}

std::vector<uint8_t> decryptSecret(const std::string &encryptedData, const std::string &password)
{
    auto data = base64Decode(encryptedData);

    if (data.size() < 16 + AES_IV_SIZE + TAG_SIZE) {
        return {};
    }

    // Extract salt, IV, ciphertext, and tag
    std::vector<uint8_t> salt(data.begin(), data.begin() + 16);
    std::vector<uint8_t> iv(data.begin() + 16, data.begin() + 16 + AES_IV_SIZE);
    std::vector<uint8_t> tag(data.end() - TAG_SIZE, data.end());
    std::vector<uint8_t> ciphertext(data.begin() + 16 + AES_IV_SIZE, data.end() - TAG_SIZE);

    std::vector<uint8_t> key(AES_KEY_SIZE);
    PKCS5_PBKDF2_HMAC(password.c_str(), password.size(), salt.data(), salt.size(),
                      PBKDF2_ITERATIONS, EVP_sha256(), AES_KEY_SIZE, key.data());

    // Initialize decryption context
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    std::vector<uint8_t> result(ciphertext.size());
    int len = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data());
    EVP_DecryptUpdate(ctx, result.data(), &len, ciphertext.data(), ciphertext.size());
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag.data());

    // Finalize decryption
    if (EVP_DecryptFinal_ex(ctx, result.data() + len, &len) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    EVP_CIPHER_CTX_free(ctx);

    return result;
}


} // namespace detail
} // namespace scorbit

// int main(int argc, char *argv[])
// {
//     if (argc != 3) {
//         std::cerr << "Usage: " << argv[0] << " <encrypted> <password>\n";
//         return 1;
//     }

//     std::string encrypted = argv[1];
//     std::string password = argv[2];

//     std::string decrypted = decryptSecret(encrypted, password);
//     std::cout << "Decrypted: " << decrypted << std::endl;

//     return 0;
// }
