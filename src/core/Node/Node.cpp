#include "Node.h"
#include <cassert>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <optional>
#include <sstream>

#include "../DiskObject/DiskView.hpp"

std::string Node::hash(const std::span<unsigned char> data) noexcept {

  alignas(32) std::array<uint8_t, 32> hash{};
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  const EVP_MD *md = EVP_sha256();

  EVP_DigestInit_ex(mdctx, md, nullptr);
  EVP_DigestUpdate(mdctx, data.data(), data.size());
  EVP_DigestFinal_ex(mdctx, hash.data(), nullptr);

  EVP_MD_CTX_free(mdctx);
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < 32; ++i)
    ss << std::setw(2) << static_cast<unsigned>(hash[i]);

  assert(ss.str().length() == 64);
  return ss.str();
}
std::string Node::hash(const std::vector<unsigned char> &data) noexcept {

  alignas(32) std::array<uint8_t, 32> hash{};
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  const EVP_MD *md = EVP_sha256();

  EVP_DigestInit_ex(mdctx, md, nullptr);
  EVP_DigestUpdate(mdctx, data.data(), data.size());
  EVP_DigestFinal_ex(mdctx, hash.data(), nullptr);

  EVP_MD_CTX_free(mdctx);
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < 32; ++i)
    ss << std::setw(2) << static_cast<unsigned>(hash[i]);

  assert(ss.str().length() == 64);
  return ss.str();
}
std::string Node::hash(const std::string_view data) noexcept {

  alignas(32) std::array<uint8_t, 32> hash{};
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  const EVP_MD *md = EVP_sha256();

  EVP_DigestInit_ex(mdctx, md, nullptr);
  EVP_DigestUpdate(mdctx, data.data(), data.size());
  EVP_DigestFinal_ex(mdctx, hash.data(), nullptr);

  EVP_MD_CTX_free(mdctx);
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < 32; ++i)
    ss << std::setw(2) << static_cast<unsigned>(hash[i]);

  assert(ss.str().length() == 64);
  return ss.str();
}
std::optional<std::string> Node::getHeadFile() noexcept {
  if (!std::filesystem::exists(HEADFILE))
    return "NOPARENT";

  DiskView file(HEADFILE);
  if (!file)
    return std::nullopt;

  return std::string(file.toSpan().cbegin(), file.toSpan().cend());
}
