#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <optional>
#include <print>
#include <vector>
#define AES_KEY_LENGTH 256
#define IV_SIZE 12
#define TAG_SIZE 16
#define LENGTH_SIZE 32
struct SSLDeleter {
  constexpr void operator()(EVP_PKEY *key) {
    if (key)
      EVP_PKEY_free(key);
  }
  constexpr void operator()(EVP_PKEY_CTX *ctx) {
    if (ctx)
      EVP_PKEY_CTX_free(ctx);
  }
  constexpr void operator()(EVP_CIPHER_CTX *ctx) {
    if (ctx)
      EVP_CIPHER_CTX_free(ctx);
  }
};

std::array<unsigned char, LENGTH_SIZE> toArray(unsigned n);

uint32_t toNumber(const std::array<unsigned char, LENGTH_SIZE> &vec);
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

struct SSLString {
  SSLString() = delete;
  SSLString(unsigned char *data, const size_t length) : _length(length) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("Unable to clone string\n");
    }
    memcpy(const_cast<unsigned char *>(this->_data), data, this->_length);
  }
  SSLString(SSLString &&other) : _data(other._data), _length(other._length) {
    other._data = nullptr;
  }

  explicit SSLString(std::string &v) : _length(v.length()) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("Unable to clone string\n");
    }
    memcpy(const_cast<unsigned char *>(this->_data), v.data(), this->_length);
  }

  explicit SSLString(std::string_view v) : _length(v.length()) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("Unable to clone string\n");
    }
    memcpy(const_cast<unsigned char *>(this->_data), v.data(), this->_length);
  }
  SSLString(const std::vector<unsigned char> &v) : _length(v.size()) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("Unable to clone string\n");
    }
    memcpy(const_cast<unsigned char *>(this->_data), v.data(), this->_length);
  }

  SSLString(const SSLString &other) : _length(other._length) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("Unable to clone string\n");
    }
    memcpy(const_cast<unsigned char *>(this->_data), other._data,
           this->_length);
  }
  ~SSLString() {
    if (this->_data)
      OPENSSL_free(const_cast<unsigned char *>(this->_data));
  }
  friend std::ostream &operator<<(std::ostream &os, const SSLString &str) {
    return os << std::string(
               reinterpret_cast<char *>(const_cast<unsigned char *>(str._data)),
               str._length);
  }
  std::string toString() {
    std::string str;
    str.resize(this->_length, 0);
    memcpy(str.data(), this->_data, this->_length);
    return str;
  }

  SSLString &operator=(const SSLString &) = delete;
  SSLString &operator=(SSLString &&) = delete;
  operator unsigned char const *() const { return this->_data; }
  const unsigned char *_data = nullptr;
  const size_t _length = 0;
};

class CypherKey {
public:
  virtual ~CypherKey() = default;
  // virtual std::optional<SSLString> encryptBlob(const std::string &data) = 0;
  virtual std::optional<SSLString> encryptBlob(const SSLString &data) = 0;
  // virtual std::optional<SSLString> decryptBlob(std::string &data) = 0;
  virtual std::optional<SSLString> decryptBlob(SSLString &data) = 0;
};

class AESKey final : public CypherKey {
public:
  AESKey() {
    if (RAND_bytes(this->_key.data(), 32) < 1) {
      std::cerr << "Unable to generate random AES Key\n";
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("AES KEY\n");
    }
  }
  AESKey(const std::span<unsigned char, 32> bytes) {
    memcpy(this->_key.data(), bytes.data(), 32);
  }
  AESKey(const unsigned char *bytes) { memcpy(this->_key.data(), bytes, 32); }
  ~AESKey() override = default;

  std::optional<SSLString> encryptBlob(const SSLString &data) override;
  // std::optional<SSLString> encryptBlob(const std::string &data) override;
  // std::optional<SSLString> decryptBlob(std::string &data) override;
  std::optional<SSLString> decryptBlob(SSLString &data) override;
  std::span<unsigned char, 32> getKey() { return this->_key; }

private:
  AESCtxPtr loadDecryptCtx(AESIV &iv);
  AESCtxPtr loadEncryptCtx(AESIV &iv);
  AESKeyBuf _key;
  std::optional<AESIV> getIV();
};

class RSAKey final : public CypherKey {
public:
  RSAKey(const std::string_view path, bool isPrivateKey = false)
      : _isPrivateKey(isPrivateKey) {
    if (this->_isPrivateKey) {
      if (auto keyOpt = loadPrivateKey(path); !keyOpt)
        throw std::runtime_error("Unable to load private key\n");
      else
        this->_key.swap(keyOpt);
    } else {
      if (auto keyOpt = loadPublicKey(path); !keyOpt)
        throw std::runtime_error("Unable to load public key\n");
      else
        this->_key.swap(keyOpt);
    }
  }
  ~RSAKey() override = default;

private:
  KeyPtr loadPublicKey(const std::string_view path);
  KeyPtr loadPrivateKey(const std::string_view path);

  RSACtxPtr loadEncryptCtx();
  RSACtxPtr loadDecryptCtx();

public:
  // std::optional<SSLString> encryptBlob(const std::string &data) override;
  std::optional<SSLString> encryptBlob(const SSLString &data) override;
  std::optional<SSLString> decryptBlob(SSLString &data) override;
  // std::optional<SSLString> decryptBlob(std::string &data) override;

private:
  KeyPtr _key = nullptr;
  bool _isPrivateKey = false;
};
