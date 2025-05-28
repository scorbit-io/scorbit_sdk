/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Feb 2025
 *
 ****************************************************************************/

#include <obfuscate.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <string>
#include <openssl/buffer.h>
#include <openssl/kdf.h>

constexpr int AES_KEY_SIZE = 32; // AES-256
constexpr int AES_IV_SIZE = 12;
constexpr int TAG_SIZE = 16;
constexpr int PBKDF2_ITERATIONS = 10000;

std::string base64Encode(const std::vector<uint8_t> &data)
{
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data.data(), data.size());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string encoded(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    return encoded;
}

std::string encryptSecret(const std::vector<uint8_t> &secret, const std::string &password)
{
    std::vector<uint8_t> salt(16);
    RAND_bytes(salt.data(), salt.size()); // Generate salt for key derivation

    std::vector<uint8_t> key(AES_KEY_SIZE);
    PKCS5_PBKDF2_HMAC(password.c_str(), password.size(), salt.data(), salt.size(),
                      PBKDF2_ITERATIONS, EVP_sha256(), AES_KEY_SIZE, key.data());

    std::vector<uint8_t> iv(AES_IV_SIZE);
    RAND_bytes(iv.data(), AES_IV_SIZE); // Generate random IV

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    std::vector<uint8_t> ciphertext(secret.size());
    std::vector<uint8_t> tag(TAG_SIZE);

    int len = 0;
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data());
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, static_cast<const uint8_t *>(secret.data()),
                      secret.size());
    int ciphertext_len = len;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data());
    EVP_CIPHER_CTX_free(ctx);

    // Store salt, IV, ciphertext, and tag
    std::vector<uint8_t> combined = std::move(salt);
    combined.insert(combined.end(), iv.begin(), iv.end());
    combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
    combined.insert(combined.end(), tag.begin(), tag.end());

    return base64Encode(combined);
}

std::vector<uint8_t> hex2bytes(const std::string &hex)
{
    std::vector<uint8_t> bytes;
    constexpr auto strKeySize = AES_KEY_SIZE * 2;

    const auto hexExample = "Expected a hex string of " + std::to_string(strKeySize)
                          + " characters.\n"
                            "E.g: 07d54d8b3b2743550071639fda6f5ed7bb0407c479fda3a5cdbd22b09870dcf9";

    if (hex.length() != strKeySize) {
        throw std::invalid_argument("Invalid key length: " + std::to_string(hex.length()) + "\n"
                                    + hexExample);
    }

    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        std::size_t pos;
        uint8_t byte = static_cast<uint8_t>(std::stoul(byteString, &pos, 16));
        if (pos != 2) {
            throw std::invalid_argument("Invalid characters found in the key: " + byteString
                                        + "\n" + hexExample);
        }
        bytes.push_back(byte);
    }

    return bytes;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <key> <provider>\n";
        std::cerr << "Example:\n"
                  << argv[0]
                  << " 07d54d8b3b2743550071639fda6f5ed7bb0407c479fda3a5cdbd22b09870dcf9 my_provider"
                  << std::endl;
        return 1;
    }

    try {
        const auto secret = hex2bytes(argv[1]);
        std::string provider = argv[2] + std::string(AY_OBFUSCATE(SCORBIT_SDK_ENCRYPT_SECRET));

        std::string encrypted = encryptSecret(secret, provider);
        std::cout << encrypted << std::endl;
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
