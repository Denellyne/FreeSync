#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <optional>
#include <vector>
#define AES_KEY_LENGTH 256
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

class AESKey;
class RSAKey;
typedef std::array<unsigned char, 32> AESKeyBuf;
typedef std::array<unsigned char, 16> AESIV;
typedef std::unique_ptr<EVP_PKEY, SSLDeleter> KeyPtr;
typedef std::unique_ptr<EVP_PKEY_CTX, SSLDeleter> RSACtxPtr;
typedef std::unique_ptr<EVP_CIPHER_CTX, SSLDeleter> AESCtxPtr;
typedef std::unique_ptr<AESKey> AESPtr;
typedef std::unique_ptr<RSAKey> RSAPtr;

struct SSLString {
  SSLString() = delete;
  SSLString(unsigned char *data, const size_t length)
      : _data(data), _length(length) {}
  SSLString(SSLString &&other) : _data(other._data), _length(other._length) {
    other._data = nullptr;
  }

  explicit SSLString(std::string &v) : _length(v.length() + 1) {
    if (this->_data = (unsigned char *)OPENSSL_malloc(this->_length);
        !this->_data) {
      std::cerr << "Unable to allocate memory for cipherText blob\n";
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("Unable to clone string\n");
    }
    memcpy(const_cast<unsigned char *>(this->_data), v.data(), this->_length);
  }
  SSLString(std::vector<unsigned char> &v) : _length(v.size()) {
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
  virtual std::optional<SSLString> encryptBlob(const std::string &data) = 0;

  virtual std::optional<SSLString> encryptBlob(const SSLString &data) = 0;
  virtual std::optional<SSLString> decryptBlob(std::string &data) = 0;
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

  virtual std::optional<SSLString> encryptBlob(const SSLString &data) override;
  virtual std::optional<SSLString>
  encryptBlob(const std::string &data) override;
  virtual std::optional<SSLString> decryptBlob(std::string &data) override;
  virtual std::optional<SSLString> decryptBlob(SSLString &data) override;

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
  virtual ~RSAKey() override = default;

private:
  KeyPtr loadPublicKey(const std::string_view path);
  KeyPtr loadPrivateKey(const std::string_view path);

  RSACtxPtr loadEncryptCtx();
  RSACtxPtr loadDecryptCtx();

public:
  virtual std::optional<SSLString>
  encryptBlob(const std::string &data) override;
  virtual std::optional<SSLString> encryptBlob(const SSLString &data) override;
  virtual std::optional<SSLString> decryptBlob(SSLString &data) override;
  virtual std::optional<SSLString> decryptBlob(std::string &data) override;

private:
  KeyPtr _key = nullptr;
  bool _isPrivateKey = false;
};
