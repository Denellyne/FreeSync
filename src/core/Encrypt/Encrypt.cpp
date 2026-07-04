#include "Encrypt.h"

std::optional<SSLString> AESKey::encryptBlob(const std::string &data) {
  AESIV iv;
  if (auto ivOpt = getIV(); !ivOpt.has_value()) {
    std::cerr << "Unable to get IV to use for encryption\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else
    iv = ivOpt.value();

  AESCtxPtr ctx = loadEncryptCtx(iv);
  if (!ctx)
    return std::nullopt;

  int cLen = data.length() + AES_BLOCK_SIZE, fLen = 0;
  if (unsigned char *cipherText = (unsigned char *)OPENSSL_malloc(cLen + 16);
      !cipherText) {
    std::cerr << "Unable to allocate memory for cipherText blob\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else {
    if (EVP_EncryptUpdate(
            ctx.get(), &cipherText[16], &cLen,
            reinterpret_cast<unsigned char *>(const_cast<char *>(data.data())),
            data.length()) <= 0) {
      OPENSSL_free(cipherText);
      std::cerr << "Unable to update encrypted blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    if (EVP_EncryptFinal_ex(ctx.get(), &cipherText[16] + cLen, &fLen) <= 0) {
      OPENSSL_free(cipherText);
      std::cerr << "Unable to finish encrypting blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    const size_t len = cLen + fLen + 16;
    memcpy(cipherText, iv.data(), 16);
    return SSLString(cipherText, len);
  }
}

std::optional<SSLString> AESKey::decryptBlob(std::string &data) {

  const std::string ivString = data.substr(0, 16);
  AESIV iv{};
  memmove(iv.data(), ivString.data(), 16);
  AESCtxPtr ctx = loadDecryptCtx(iv);
  if (!ctx)
    return std::nullopt;

  int cLen = data.length() - 16, fLen = 0;
  if (unsigned char *plainText = (unsigned char *)OPENSSL_malloc(cLen);
      !plainText) {
    std::cerr << "Unable to allocate memory for plainText blob\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else {
    if (EVP_DecryptUpdate(ctx.get(), plainText, &cLen,
                          reinterpret_cast<unsigned char *>(&data.data()[16]),
                          data.length() - 16) <= 0) {
      OPENSSL_free(plainText);
      std::cerr << "Unable to update encrypted blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    if (EVP_DecryptFinal_ex(ctx.get(), plainText + cLen, &fLen) <= 0) {
      OPENSSL_free(plainText);
      std::cerr << "Unable to finish encrypting blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    const size_t len = cLen + fLen;
    return SSLString(plainText, len);
  }
};

std::optional<SSLString> AESKey::decryptBlob(SSLString &data) {

  AESIV iv{};
  memcpy(iv.data(), data._data, 16);
  AESCtxPtr ctx = loadDecryptCtx(iv);
  if (!ctx)
    return std::nullopt;

  int cLen = data._length - 16, fLen = 0;

  if (unsigned char *plainText = (unsigned char *)OPENSSL_malloc(cLen);
      !plainText) {
    std::cerr << "Unable to allocate memory for plainText blob\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else {
    if (EVP_DecryptUpdate(ctx.get(), plainText, &cLen, &data._data[16],
                          data._length - 16) <= 0) {
      OPENSSL_free(plainText);
      std::cerr << "Unable to update encrypted blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    if (EVP_DecryptFinal_ex(ctx.get(), plainText + cLen, &fLen) <= 0) {
      OPENSSL_free(plainText);
      std::cerr << "Unable to finish encrypting blob\n";
      ERR_print_errors_fp(stderr);
      return std::nullopt;
    }

    const size_t len = cLen + fLen;
    return SSLString(plainText, len);
  }
};

AESCtxPtr AESKey::loadDecryptCtx(AESIV &iv) {
  AESCtxPtr ctx = AESCtxPtr(EVP_CIPHER_CTX_new());
  if (!ctx)
    return nullptr;

  if (!EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_cbc(), NULL, this->_key.data(),
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

  if (!EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_cbc(), NULL, this->_key.data(),
                          iv.data())) {
    std::cerr << "Unable to initialize context\n";
    ERR_print_errors_fp(stderr);
    return nullptr;
  }
  return ctx;
}

std::optional<AESIV> AESKey::getIV() {
  AESIV iv{};
  if (RAND_bytes(iv.data(), 16) < 1) {
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
  std::cout << data << '\n';
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

  std::cerr << "Info " << outlen << " " << inlen << '\n';
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
std::optional<SSLString> RSAKey::encryptBlob(const std::string &data) {
  assert(this->_isPrivateKey == false);
  RSACtxPtr ctx = nullptr;
  if (ctx = loadEncryptCtx(); !ctx) {
    std::cerr << "Unable to get encrypted blob length\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }
  const size_t inlen = data.length();
  size_t outlen = 0;
  if (EVP_PKEY_encrypt(
          ctx.get(), nullptr, &outlen,
          reinterpret_cast<unsigned char *>(const_cast<char *>(data.data())),
          inlen) <= 0) {
    std::cerr << "Unable to get encrypted blob length\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }

  if (unsigned char *out = (unsigned char *)OPENSSL_malloc(outlen); !out) {
    std::cerr << "Unable to allocate string of size " << outlen << '\n';
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else if (EVP_PKEY_encrypt(ctx.get(), out, &outlen,
                              reinterpret_cast<unsigned char *>(
                                  const_cast<char *>(data.data())),
                              inlen) <= 0) {
    std::cerr << "Unable to encrypt blob\n";
    OPENSSL_free(out);
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else
    return SSLString(out, outlen);
}

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

std::optional<SSLString> RSAKey::decryptBlob(std::string &data) {
  assert(this->_isPrivateKey);
  RSACtxPtr ctx = nullptr;
  if (ctx = loadDecryptCtx(); !ctx) {
    std::cerr << "Unable to get encrypted blob length\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }
  const size_t inlen = data.length();
  size_t outlen = 0;
  if (EVP_PKEY_decrypt(ctx.get(), nullptr, &outlen,
                       reinterpret_cast<unsigned char *>(data.data()),
                       inlen) <= 0) {
    std::cerr << "Unable to get decrypted blob length\n";
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  }

  if (unsigned char *out = (unsigned char *)OPENSSL_malloc(outlen); !out) {
    std::cerr << "Unable to allocate string of size " << outlen << '\n';
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else if (EVP_PKEY_decrypt(ctx.get(), out, &outlen,
                              reinterpret_cast<unsigned char *>(data.data()),
                              inlen) <= 0) {
    std::cerr << "Unable to decrypt blob\n";
    OPENSSL_free(out);
    ERR_print_errors_fp(stderr);
    return std::nullopt;
  } else
    return SSLString(out, outlen);
}
