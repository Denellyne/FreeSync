#include "Encrypt.h"
#include <openssl/aes.h>
#include <optional>

std::optional<SSLString> AESKey::encryptBlob(const SSLString &data) {
  if (data._length == 0)
    return std::nullopt;
  AESIV iv;
  AESTAG tag;
  if (auto ivOpt = getIV(); !ivOpt.has_value()) {
    std::cerr << "Unable to get IV to use for encryption\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else
    iv = ivOpt.value();

  AESCtxPtr ctx = loadEncryptCtx(iv);
  if (!ctx)
    return std::nullopt;

  int len = data._length;
  if (unsigned char *cipherText = (unsigned char *)OPENSSL_malloc(len);
      !cipherText) {
    std::cerr << "Unable to allocate memory for cipherText blob\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else {
    if (EVP_EncryptUpdate(ctx.get(), cipherText, &len, data._data,
                          data._length) <= 0) {
      OPENSSL_free(cipherText);
      std::cerr << "Unable to update encrypted blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }
    int cLen = len;

    if (EVP_EncryptFinal_ex(ctx.get(), cipherText + len, &len) <= 0) {
      OPENSSL_free(cipherText);
      std::cerr << "Unable to finish encrypting blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }
    cLen += len;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TAG_SIZE,
                            tag.data()) <= 0) {
      OPENSSL_free(cipherText);
      std::cerr << "Unable to ctx ctrl\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    unsigned char *packet =
        (unsigned char *)OPENSSL_malloc(IV_SIZE + cLen + TAG_SIZE);
    if (!packet) {
      OPENSSL_free(cipherText);
      std::cerr << "Unable to malloc memory for final packet";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }
    memcpy(packet, iv.data(), IV_SIZE);
    memcpy(packet + IV_SIZE, cipherText, cLen);
    OPENSSL_free(cipherText);
    memcpy(packet + IV_SIZE + cLen, tag.data(), tag.size());
    return SSLString(packet, cLen + IV_SIZE + TAG_SIZE);
  }
}

std::optional<SSLString> AESKey::decryptBlob(SSLString &data) {
  AESIV iv{};
  AESTAG tag{};
  if (data._length < IV_SIZE + TAG_SIZE)
    return std::nullopt;

  const uint32_t size = data._length - IV_SIZE - TAG_SIZE;
  memcpy(iv.data(), data._data, IV_SIZE);
  memcpy(tag.data(), data._data + IV_SIZE + size, TAG_SIZE);
  AESCtxPtr ctx = loadDecryptCtx(iv);
  if (!ctx)
    return std::nullopt;

  int len = 0;

  if (unsigned char *plainText = (unsigned char *)OPENSSL_malloc(size);
      !plainText) {
    std::cerr << "Unable to allocate memory for plainText blob\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else {
    if (EVP_DecryptUpdate(ctx.get(), plainText, &len, data._data + IV_SIZE,
                          size) <= 0) {
      OPENSSL_free(plainText);
      std::cerr << "Unable to update decrypted blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    int cLen = len;
    if (!EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                             tag.data())) {
      OPENSSL_free(plainText);
      std::cerr << "Unable to set tag\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    if (EVP_DecryptFinal_ex(ctx.get(), plainText + len, &len) <= 0) {
      OPENSSL_free(plainText);
      std::cerr << "Unable to finish decrypting blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }
    cLen += len;

    return SSLString(plainText, cLen);
  }
};
AESCtxPtr AESKey::loadDecryptCtx(AESIV &iv) {
  AESCtxPtr ctx = AESCtxPtr(EVP_CIPHER_CTX_new());
  if (!ctx)
    return nullptr;

  if (!EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr,
                          nullptr)) {
    std::cerr << "Unable to initialize context\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }
  if (!EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, IV_SIZE,
                           nullptr)) {
    std::cerr << "Unable to initialize context\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }

  if (!EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, this->_key.data(),
                          iv.data())) {
    std::cerr << "Unable to initialize context\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }
  return ctx;
}

AESCtxPtr AESKey::loadEncryptCtx(AESIV &iv) {
  AESCtxPtr ctx = AESCtxPtr(EVP_CIPHER_CTX_new());
  if (!ctx)
    return nullptr;

  if (!EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr,
                          nullptr)) {
    std::cerr << "Unable to initialize context\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }
  if (!EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, IV_SIZE,
                           nullptr)) {
    std::cerr << "Unable to initialize context\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }

  if (!EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, this->_key.data(),
                          iv.data())) {
    std::cerr << "Unable to initialize context\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }
  return ctx;
}

std::optional<AESIV> AESKey::getIV() {
  AESIV iv{};
  if (RAND_bytes(iv.data(), IV_SIZE) < 1) {
    std::cerr << "Unable to generate random IV\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }
  return iv;
}

KeyPtr RSAKey::loadPublicKey(const std::string_view path) {
  if (FILE *fp = fopen(path.data(), "r"); fp) {
    KeyPtr key = KeyPtr(PEM_read_PUBKEY(fp, nullptr, nullptr, nullptr));
    fclose(fp);
    return key;
  }
  std::cerr << "Unable to read Public Key from " << path << '\n';
  ERR_print_errors_fp(stderr);
  return nullptr;
}

KeyPtr RSAKey::loadPrivateKey(const std::string_view path) {
  if (FILE *fp = fopen(path.data(), "r"); fp) {
    KeyPtr key = KeyPtr(PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr));
    fclose(fp);
    return key;
  }
  std::cerr << "Unable to read Private Key from " << path << '\n';
  ERR_print_errors_fp(stderr);
  return nullptr;
}
KeyPtr RSAKey::loadPublicKey(const std::vector<unsigned char> &key) {
  BIO *bp(BIO_new_mem_buf((void *)key.data(), key.size()));
  if (!bp) {
    std::cerr << "Unable to load public key from buffer\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }

  KeyPtr keyPtr = KeyPtr(PEM_read_bio_PUBKEY(bp, nullptr, nullptr, nullptr));
  BIO_free(bp);
  return keyPtr;
}
KeyPtr RSAKey::loadPrivateKey(const std::vector<unsigned char> &key) {
  BIO *bp(BIO_new_mem_buf((void *)key.data(), key.size()));
  if (!bp) {
    std::cerr << "Unable to load private key from buffer\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }

  KeyPtr keyPtr =
      KeyPtr(PEM_read_bio_PrivateKey(bp, nullptr, nullptr, nullptr));
  BIO_free(bp);
  return keyPtr;
}
RSACtxPtr RSAKey::loadEncryptCtx() {
  assert(this->_key != nullptr);
  if (EVP_PKEY_CTX *ctxRaw = EVP_PKEY_CTX_new(this->_key.get(), nullptr);
      !ctxRaw) {
    std::cerr << "Unable to initialize context from key\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  } else if (EVP_PKEY_encrypt_init(ctxRaw) <= 0) {
    std::cerr << "Unable to init encrypt function\n";
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(ctxRaw);
    return nullptr;
  } else if (EVP_PKEY_CTX_set_rsa_padding(ctxRaw, RSA_PKCS1_OAEP_PADDING) <=
             0) {
    std::cerr << "Unable to set RSA padding\n";
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(ctxRaw);
    return nullptr;
  } else if (EVP_PKEY_CTX_set_rsa_oaep_md(ctxRaw, EVP_sha256()) <= 0) {
    std::cerr << "Unable to set RSA padding\n";
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(ctxRaw);
    return nullptr;
  } else if (EVP_PKEY_CTX_set_rsa_mgf1_md(ctxRaw, EVP_sha256()) <= 0) {
    std::cerr << "Unable to set RSA padding\n";
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(ctxRaw);
    return nullptr;
  }

  else
    return RSACtxPtr(ctxRaw);
}
RSACtxPtr RSAKey::loadDecryptCtx() {
  assert(this->_key != nullptr);
  if (EVP_PKEY_CTX *ctxRaw = EVP_PKEY_CTX_new(this->_key.get(), nullptr);
      !ctxRaw) {
    std::cerr << "Unable to initialize context from key\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  } else if (EVP_PKEY_decrypt_init(ctxRaw) <= 0) {
    std::cerr << "Unable to init decrypt function\n";
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(ctxRaw);
    return nullptr;
  } else if (EVP_PKEY_CTX_set_rsa_padding(ctxRaw, RSA_PKCS1_OAEP_PADDING) <=
             0) {
    std::cerr << "Unable to set RSA padding\n";
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(ctxRaw);
    return nullptr;
  } else if (EVP_PKEY_CTX_set_rsa_oaep_md(ctxRaw, EVP_sha256()) <= 0) {
    std::cerr << "Unable to set RSA padding\n";
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(ctxRaw);
    return nullptr;
  } else if (EVP_PKEY_CTX_set_rsa_mgf1_md(ctxRaw, EVP_sha256()) <= 0) {
    std::cerr << "Unable to set RSA padding\n";
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(ctxRaw);
    return nullptr;
  } else
    return RSACtxPtr(ctxRaw);
}

std::optional<SSLString> RSAKey::encryptBlob(const SSLString &data) {
  if (data._length == 0)
    return std::nullopt;
  assert(this->_isPrivateKey == false);
  RSACtxPtr ctx = nullptr;
  if (ctx = loadEncryptCtx(); !ctx) {
    std::cerr << "Unable to get encrypted blob length\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }
  const size_t inlen = data._length;
  size_t outlen = 0;
  if (EVP_PKEY_encrypt(ctx.get(), nullptr, &outlen, data._data, inlen) <= 0) {
    std::cerr << "Unable to get encrypted blob length\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }

  // std::cerr << "Info " << outlen << " " << inlen << '\n';
  if (unsigned char *out = (unsigned char *)OPENSSL_malloc(outlen); !out) {
    std::cerr << "Unable to allocate string of size " << outlen << '\n';
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else if (EVP_PKEY_encrypt(ctx.get(), out, &outlen, data._data, inlen) <=
             0) {
    std::cerr << "Unable to encrypt blob\n";
    OPENSSL_free(out);
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else
    return SSLString(out, outlen);
}
// std::optional<SSLString> RSAKey::encryptBlob(const std::string &data) {
//   assert(this->_isPrivateKey == false);
//   RSACtxPtr ctx = nullptr;
//   if (ctx = loadEncryptCtx(); !ctx) {
//     std::cerr << "Unable to get encrypted blob length\n";
//     ERR_print_errors_fp(stderr);
//     return std::nullopt;
//   }
//   const size_t inlen = data.length();
//   size_t outlen = 0;
//   if (EVP_PKEY_encrypt(
//           ctx.get(), nullptr, &outlen,
//           reinterpret_cast<unsigned char *>(const_cast<char *>(data.data())),
//           inlen) <= 0) {
//     std::cerr << "Unable to get encrypted blob length\n";
//     ERR_print_errors_fp(stderr);
//     return std::nullopt;
//   }
//
//   if (unsigned char *out = (unsigned char *)OPENSSL_malloc(outlen); !out) {
//     std::cerr << "Unable to allocate string of size " << outlen << '\n';
//     ERR_print_errors_fp(stderr);
//     return std::nullopt;
//   } else if (EVP_PKEY_encrypt(ctx.get(), out, &outlen,
//                               reinterpret_cast<unsigned char *>(
//                                   const_cast<char *>(data.data())),
//                               inlen) <= 0) {
//     std::cerr << "Unable to encrypt blob\n";
//     OPENSSL_free(out);
//     ERR_print_errors_fp(stderr);
//     return std::nullopt;
//   } else
//     return SSLString(out, outlen);
// }

std::optional<SSLString> RSAKey::decryptBlob(SSLString &data) {
  assert(this->_isPrivateKey);
  RSACtxPtr ctx = nullptr;
  if (ctx = loadDecryptCtx(); !ctx) {
    std::cerr << "Unable to get encrypted blob length\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }
  const size_t inlen = data._length;
  size_t outlen = 0;
  if (EVP_PKEY_decrypt(ctx.get(), nullptr, &outlen,
                       const_cast<unsigned char *>(data._data), inlen) <= 0) {
    std::cerr << "Unable to get decrypted blob length\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }

  if (unsigned char *out = (unsigned char *)OPENSSL_malloc(outlen); !out) {
    std::cerr << "Unable to allocate string of size " << outlen << '\n';
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else if (EVP_PKEY_decrypt(ctx.get(), out, &outlen,
                              const_cast<unsigned char *>(data._data),
                              inlen) <= 0) {
    std::cerr << "Unable to decrypt blob\n";
    OPENSSL_free(out);
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else
    return SSLString(out, outlen);
}

// std::optional<SSLString> RSAKey::decryptBlob(std::string &data) {
//   assert(this->_isPrivateKey);
//   RSACtxPtr ctx = nullptr;
//   if (ctx = loadDecryptCtx(); !ctx) {
//     std::cerr << "Unable to get encrypted blob length\n";
//     ERR_print_errors_fp(stderr);
//     return std::nullopt;
//   }
//   const size_t inlen = data.length();
//   size_t outlen = 0;
//   if (EVP_PKEY_decrypt(ctx.get(), nullptr, &outlen,
//                        reinterpret_cast<unsigned char *>(data.data()),
//                        inlen) <= 0) {
//     std::cerr << "Unable to get decrypted blob length\n";
//     ERR_print_errors_fp(stderr);
//     return std::nullopt;
//   }
//
//   if (unsigned char *out = (unsigned char *)OPENSSL_malloc(outlen); !out) {
//     std::cerr << "Unable to allocate string of size " << outlen << '\n';
//     ERR_print_errors_fp(stderr);
//     return std::nullopt;
//   } else if (EVP_PKEY_decrypt(ctx.get(), out, &outlen,
//                               reinterpret_cast<unsigned char *>(data.data()),
//                               inlen) <= 0) {
//     std::cerr << "Unable to decrypt blob\n";
//     OPENSSL_free(out);
//     ERR_print_errors_fp(stderr);
//     return std::nullopt;
//   } else
//     return SSLString(out, outlen);
// }

std::array<unsigned char, LENGTH_SIZE> toArray(uint32_t n) {
  std::array<unsigned char, LENGTH_SIZE> res{'0'};
  for (int i = 31; i >= 0; i--) {
    res[i] = (n % 10) + '0';
    n /= 10;
  }
  return res;
}
std::optional<uint32_t>
toNumber(const std::array<unsigned char, LENGTH_SIZE> &vec) {
  uint32_t num = 0;
  for (const auto c : vec)
    num = (num * 10) + (c - '0');
  return num;
}

bool RSAKey::validateBlob(const SSLString &raw, const SSLString &sig) {
  assert(!this->_isPrivateKey && this->_key);
  EVP_MD_CTX *m_RSAVerifyCtx = EVP_MD_CTX_create();
  if (!m_RSAVerifyCtx) {
    ERR_print_errors_fp(stderr);
    std::cerr << "Unable to initialize allocate context\n";
    return false;
  }

  if (EVP_DigestVerifyInit(m_RSAVerifyCtx, NULL, EVP_sha256(), NULL,
                           this->_key.get()) <= 0) {
    EVP_MD_CTX_free(m_RSAVerifyCtx);
    ERR_print_errors_fp(stderr);
    std::cerr << "Unable to initialize verifier context\n";
    return false;
  }
  if (EVP_DigestVerifyUpdate(m_RSAVerifyCtx, raw._data, raw._length) <= 0) {
    EVP_MD_CTX_free(m_RSAVerifyCtx);
    ERR_print_errors_fp(stderr);
    std::cerr << "Unable to update verify digest\n";
    return false;
  }

  if (const int AuthStatus =
          EVP_DigestVerifyFinal(m_RSAVerifyCtx, sig._data, sig._length);
      AuthStatus == 1) {
    EVP_MD_CTX_free(m_RSAVerifyCtx);
    return true;
  }
  std::cerr << "The packet is invalid\n";
  EVP_MD_CTX_free(m_RSAVerifyCtx);
  return false;
}
std::optional<SSLString> RSAKey::signBlob(const SSLString &data) {
  assert(this->_isPrivateKey && this->_key);
  if (unsigned char *out = (unsigned char *)OPENSSL_malloc(RSA_SIGNING_LENGTH);
      !out) {
    std::cerr << "Unable to allocate string of size 512\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else {
    EVP_MD_CTX *m_RSASignCtx = EVP_MD_CTX_create();
    if (!m_RSASignCtx) {
      OPENSSL_free(out);
      ERR_print_errors_fp(stderr);
      std::cerr << "Unable to initialize signing context\n";
      return std::nullopt;
    }
    if (EVP_DigestSignInit(m_RSASignCtx, NULL, EVP_sha256(), NULL,
                           this->_key.get()) <= 0) {
      EVP_MD_CTX_free(m_RSASignCtx);
      OPENSSL_free(out);
      ERR_print_errors_fp(stderr);
      std::cerr << "Unable to initialize sigining digest\n";
      return std::nullopt;
    }
    if (EVP_DigestSignUpdate(m_RSASignCtx, data._data, data._length) <= 0) {
      EVP_MD_CTX_free(m_RSASignCtx);
      OPENSSL_free(out);
      ERR_print_errors_fp(stderr);
      std::cerr << "Unable to initialize sign packet\n";
      return std::nullopt;
    }
    if (size_t encLength = 0;
        EVP_DigestSignFinal(m_RSASignCtx, NULL, &encLength) <= 0 ||
        encLength != RSA_SIGNING_LENGTH) {
      EVP_MD_CTX_free(m_RSASignCtx);
      OPENSSL_free(out);
      ERR_print_errors_fp(stderr);
      std::cerr << "Unable to finish updating signature size\n";
      return std::nullopt;
    } else if (EVP_DigestSignFinal(m_RSASignCtx, out, &encLength) <= 0) {
      EVP_MD_CTX_free(m_RSASignCtx);
      OPENSSL_free(out);
      ERR_print_errors_fp(stderr);
      std::cerr << "Unable to finish signing packet\n";
      return std::nullopt;
    }
    EVP_MD_CTX_free(m_RSASignCtx);
    return SSLString(out, RSA_SIGNING_LENGTH);
  }
}
