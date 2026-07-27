#include "Encrypt.h"
#include <openssl/aes.h>

std::optional<SSLString> AESKey::encryptBlob(const SSLString &data) {
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

    std::cout << "a\n";
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

std::array<unsigned char, LENGTH_SIZE> toArray(unsigned n) {
  std::array<unsigned char, LENGTH_SIZE> res{'0'};
  for (int i = 31; i >= 0; i--) {
    res[i] = (n % 10) + '0';
    n /= 10;
  }
  return res;
}
uint32_t toNumber(const std::array<unsigned char, LENGTH_SIZE> &vec) {
  uint32_t num = 0;
  for (const auto c : vec)
    num = (num * 10) + (c - '0');
  return num;
}
