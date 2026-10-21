#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <optional>
#include <vector>
#define AES_KEY_LENGTH 256
#define AES_KEY_BYTES 32
#define RSA_SIGNING_LENGTH 256
#define IV_SIZE 12
#define TAG_SIZE 16
#define LENGTH_SIZE 32
#define PTR_SIZE 64
struct SSLDeleter {
  constexpr void operator()(EVP_PKEY *key) noexcept {
    if (key)
      EVP_PKEY_free(key);
  }
  constexpr void operator()(EVP_PKEY_CTX *ctx) noexcept {
    if (ctx)
      EVP_PKEY_CTX_free(ctx);
  }
  constexpr void operator()(EVP_CIPHER_CTX *ctx) noexcept {
    if (ctx)
      EVP_CIPHER_CTX_free(ctx);
  }
};

struct SSLString;
std::array<unsigned char, LENGTH_SIZE> toArray(uint32_t n) noexcept;
std::array<unsigned char, PTR_SIZE> ptrToArray(uint64_t n) noexcept;

std::optional<uint32_t>
toNumber(const std::array<unsigned char, LENGTH_SIZE> &vec) noexcept;
std::optional<uint32_t> toNumber(const SSLString &str) noexcept;
std::optional<uint64_t> ptrToNumber(const SSLString &str) noexcept;

std::optional<uint32_t>
toNumber(const std::span<const unsigned char> &vec) noexcept;
std::optional<uint64_t>
ptrToNumber(const std::span<const unsigned char> &vec) noexcept;
template <typename T>
concept ByteSpan =
    requires { typename T::element_type; } &&
    std::same_as<std::remove_const_t<typename T::element_type>,
                 unsigned char> &&
    requires(T s) { []<typename U, std::size_t Ext>(std::span<U, Ext>) {}(s); };
class AESKey;
class RSAKey;
typedef std::array<unsigned char, 32> AESKeyBuf;
typedef std::array<unsigned char, IV_SIZE> AESIV;
typedef std::array<unsigned char, TAG_SIZE> AESTAG;
typedef std::unique_ptr<EVP_PKEY, SSLDeleter> KeyPtr;
typedef std::unique_ptr<EVP_PKEY_CTX, SSLDeleter> RSACtxPtr;
typedef std::unique_ptr<EVP_CIPHER_CTX, SSLDeleter> AESCtxPtr;
typedef std::unique_ptr<AESKey> AESPtr;
typedef std::unique_ptr<RSAKey> RSAPtr;
typedef std::shared_ptr<RSAKey> RSASPtr;

struct SSLString {
  SSLString() = delete;
  explicit SSLString(const unsigned char *data, const size_t length,
                     const bool isSSLAllocated = true) noexcept
      : _length(length) {
    if (isSSLAllocated)
      this->_data = data;
    else {
      if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
          !this->_data) {
        std::cerr << "Unable to allocate memory for cipherText blob\n";
        ERR_print_errors_fp(stderr);
      }
      memcpy(const_cast<unsigned char *>(this->_data), data, this->_length);
    }
  }
  SSLString(SSLString &&other) noexcept
      : _data(other._data), _length(other._length) {
    other._data = nullptr;
  }

  explicit SSLString(const std::string &v) noexcept : _length(v.length()) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
    }
    memcpy(const_cast<unsigned char *>(this->_data), v.data(), this->_length);
  }

  explicit SSLString(const std::string_view v) noexcept : _length(v.length()) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
    }
    memcpy(const_cast<unsigned char *>(this->_data), v.data(), this->_length);
  }
  explicit SSLString(const std::vector<unsigned char> &v) noexcept
      : _length(v.size()) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
    }
    memcpy(const_cast<unsigned char *>(this->_data), v.data(), this->_length);
  }
  explicit SSLString(const std::span<unsigned char> v) noexcept
      : _length(v.size()) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
    }
    memcpy(const_cast<unsigned char *>(this->_data), v.data(), this->_length);
  }

  // explicit SSLString(const SSLString &other) noexcept :
  // _length(other._length) {
  //   if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
  //       !this->_data) {
  //     std::cerr << "Unable to allocate memory for cipherText blob\n";
  //     ERR_print_errors_fp(stderr);
  //   }
  //   memcpy(const_cast<unsigned char *>(this->_data), other._data,
  //          this->_length);
  // }
  ~SSLString() noexcept {
    if (this->_data)
      OPENSSL_free(const_cast<unsigned char *>(this->_data));
  }
  friend std::ostream &operator<<(std::ostream &os,
                                  const SSLString &str) noexcept {
    return os << std::string(
               reinterpret_cast<char *>(const_cast<unsigned char *>(str._data)),
               str._length);
  }
  std::string toString() const noexcept {
    std::string str;
    str.resize(this->_length, 0);
    memcpy(str.data(), this->_data, this->_length);
    return str;
  }

  SSLString &operator=(const SSLString &) = delete;
  SSLString &operator=(SSLString &&other) noexcept {
    if (this->_data)
      OPENSSL_free(const_cast<unsigned char *>(this->_data));

    this->_data = other._data;
    const_cast<size_t &>(this->_length) = other._length;
    const_cast<size_t &>(other._length) = 0;
    other._data = nullptr;
    return *this;
  }
  operator unsigned char const *() const noexcept { return this->_data; }
  operator bool() const noexcept { return this->_data != nullptr; }
  const unsigned char *_data = nullptr;
  const size_t _length = 0;
};

class CypherKey {
public:
  virtual ~CypherKey() noexcept = default;
  // virtual std::optional<SSLString> encryptBlob(const std::string &data) = 0;
  virtual std::optional<SSLString>
  encryptBlob(const SSLString &data) noexcept = 0;
  // virtual std::optional<SSLString> decryptBlob(std::string &data) = 0;
  virtual std::optional<SSLString> decryptBlob(SSLString &data) noexcept = 0;

  explicit operator bool() const noexcept { return !this->_error; }
  bool isOk() const noexcept { return !this->_error; };

protected:
  bool _error = false;
};

class AESKey final : public CypherKey {
public:
  explicit AESKey() noexcept {
    if (RAND_bytes(this->_key.data(), 32) < 1) [[unlikely]] {
      std::cerr << "Unable to generate random AES Key\n";
      ERR_print_errors_fp(stderr);
      memset(this->_key.data(), 0, 32);
      this->_error = true;
    }
  }
  explicit AESKey(const std::span<unsigned char, 32> bytes) noexcept {
    memcpy(this->_key.data(), bytes.data(), 32);
  }
  explicit AESKey(const unsigned char *bytes) noexcept {
    memcpy(this->_key.data(), bytes, 32);
  }
  ~AESKey() noexcept override = default;

  std::optional<SSLString> encryptBlob(const SSLString &data) noexcept override;
  // std::optional<SSLString> encryptBlob(const std::string &data) override;
  // std::optional<SSLString> decryptBlob(std::string &data) override;
  std::optional<SSLString> decryptBlob(SSLString &data) noexcept override;
  std::span<unsigned char, 32> getKey() noexcept { return this->_key; }

private:
  AESCtxPtr loadDecryptCtx(AESIV &iv) noexcept;
  AESCtxPtr loadEncryptCtx(AESIV &iv) noexcept;
  alignas(32) AESKeyBuf _key;
  static std::optional<AESIV> getIV() noexcept;
};

class RSAKey final : public CypherKey {
public:
  RSAKey() = delete;
  explicit RSAKey(const std::span<unsigned char> key,
                  bool isPrivateKey = false) noexcept
      : _isPrivateKey(isPrivateKey) {
    if (this->_isPrivateKey) {
      if (auto keyOpt = loadPrivateKey(key); !keyOpt) [[unlikely]]
        this->_error = true;
      else
        this->_key.swap(keyOpt);
    } else {
      if (auto keyOpt = loadPublicKey(key); !keyOpt) [[unlikely]]
        this->_error = true;
      else
        this->_key.swap(keyOpt);
    }
  }
  explicit RSAKey(const std::string_view path,
                  bool isPrivateKey = false) noexcept
      : _isPrivateKey(isPrivateKey) {
    if (this->_isPrivateKey) {
      if (auto keyOpt = loadPrivateKey(path); !keyOpt) [[unlikely]]
        this->_error = true;
      else
        this->_key.swap(keyOpt);
    } else {
      if (auto keyOpt = loadPublicKey(path); !keyOpt) [[unlikely]]
        this->_error = true;
      else
        this->_key.swap(keyOpt);
    }
  }
  ~RSAKey() noexcept override = default;

private:
  KeyPtr loadPublicKey(const std::string_view path) noexcept;
  KeyPtr loadPrivateKey(const std::string_view path) noexcept;
  KeyPtr loadPublicKey(const std::span<unsigned char> key) noexcept;
  KeyPtr loadPrivateKey(const std::span<unsigned char> key) noexcept;

  RSACtxPtr loadEncryptCtx() noexcept;
  RSACtxPtr loadDecryptCtx() noexcept;

public:
  // std::optional<SSLString> encryptBlob(const std::string &data) override;
  std::optional<SSLString> encryptBlob(const SSLString &data) noexcept override;
  std::optional<SSLString> decryptBlob(SSLString &data) noexcept override;
  std::optional<SSLString> signBlob(const SSLString &data) noexcept;
  bool validateBlob(const SSLString &raw, const SSLString &sig) noexcept;

  // std::optional<SSLString> decryptBlob(std::string &data) override;

private:
  KeyPtr _key = nullptr;

public:
  bool _isPrivateKey = false;
};
