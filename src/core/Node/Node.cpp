#include "Node.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <optional>
#include <sstream>

std::string Node::hash(const std::vector<unsigned char> &data) {

  std::array<uint8_t, 32> hash{};
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  const EVP_MD *md = EVP_sha256();

  EVP_DigestInit_ex(mdctx, md, nullptr);
  EVP_DigestUpdate(mdctx, data.data(), data.size());
  EVP_DigestFinal_ex(mdctx, hash.data(), nullptr);

  EVP_MD_CTX_free(mdctx);
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < 32; ++i) {
    ss << std::setw(2) << static_cast<unsigned>(hash[i]);
  }

  assert(ss.str().length() == 64);
  return ss.str();
}
std::string Node::hash(const std::string_view data) {

  std::array<uint8_t, 32> hash{};
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  const EVP_MD *md = EVP_sha256();

  EVP_DigestInit_ex(mdctx, md, nullptr);
  EVP_DigestUpdate(mdctx, data.data(), data.size());
  EVP_DigestFinal_ex(mdctx, hash.data(), nullptr);

  EVP_MD_CTX_free(mdctx);
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < 32; ++i) {
    ss << std::setw(2) << static_cast<unsigned>(hash[i]);
  }

  assert(ss.str().length() == 64);
  return ss.str();
}
std::optional<std::string> Node::getHeadFile() {

  if (!std::filesystem::exists(HEADFILE))
    return "NOPARENT";
  std::string data;
  std::ifstream file(HEADFILE, std::fstream::binary);
  if (!file) {
    throw std::runtime_error("Unable to create leaf node\n");
    return std::nullopt;
  }

  file.seekg(0, file.end);
  const int length = file.tellg();
  data.resize(length);
  file.seekg(0, file.beg);

  file.read(reinterpret_cast<char *>(data.data()), length);
  return data;
}
