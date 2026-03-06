/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/buffer.h>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>

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


std::string encryptSecret(const std::vector<uint8_t> &data, const std::string &password)
{
    std::vector<uint8_t> salt(16);
    RAND_bytes(salt.data(), salt.size());

    std::vector<uint8_t> key(AES_KEY_SIZE);
    PKCS5_PBKDF2_HMAC(password.c_str(), password.size(), salt.data(), salt.size(),
                      PBKDF2_ITERATIONS, EVP_sha256(), AES_KEY_SIZE, key.data());

    std::vector<uint8_t> iv(AES_IV_SIZE);
    RAND_bytes(iv.data(), AES_IV_SIZE);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    std::vector<uint8_t> ciphertext(data.size());
    std::vector<uint8_t> tag(TAG_SIZE);

    int len = 0;
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data());
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, data.data(), data.size());
    int ciphertextLen = len;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertextLen += len;
    ciphertext.resize(ciphertextLen);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data());
    EVP_CIPHER_CTX_free(ctx);

    std::vector<uint8_t> combined = std::move(salt);
    combined.insert(combined.end(), iv.begin(), iv.end());
    combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
    combined.insert(combined.end(), tag.begin(), tag.end());

    return base64Encode(combined);
}

std::string computeHmac(const std::string &message, const std::string &key)
{
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int resultLen = 0;

    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char *>(message.data()), message.size(), result,
         &resultLen);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < resultLen; ++i) {
        oss << std::setw(2) << static_cast<int>(result[i]);
    }
    return oss.str();
}

} // namespace detail
} // namespace scorbit
