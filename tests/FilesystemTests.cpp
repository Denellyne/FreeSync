#include "../src/core/DiskObject/DiskView.hpp"
#include "../src/core/Node/LTree.h"
#include "../src/core/Node/Tree.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <filesystem>
#include <random>
#include <stdio.h>

namespace fs = std::filesystem;
constexpr std::vector<unsigned char> generateRandomData(std::mt19937_64 &gen) {
  std::vector<unsigned char> data{};
  data.emplace_back('a');
  const size_t len = 1 + gen() % 4095;

  for (size_t i = 0; i < len; i++)
    data.emplace_back(gen());
  return data;
}

void generateRandomDirectory(const std::string_view path, int depth = 0) {
  static std::mt19937_64 gen(time(NULL));
  if (depth == 4)
    return;

  const size_t len = 1 + gen() % 15;
  for (size_t i = 0; i < len; i++) {
    if (gen() % 2 == 0) {
      std::string TMP_TEMPLATE{path};
      TMP_TEMPLATE += "/XXXXXX";
      ASSERT_NE(mkdtemp(TMP_TEMPLATE.data()), nullptr);
      ASSERT_NO_FATAL_FAILURE(generateRandomDirectory(TMP_TEMPLATE, depth + 1));
    } else {
      std::string TMP_TEMPLATE{path};
      TMP_TEMPLATE += "/XXXXXX";
      int fd = mkstemp(TMP_TEMPLATE.data());
      ASSERT_GE(fd, 0);
      close(fd);
      DiskFile file(TMP_TEMPLATE, true);
      auto data = generateRandomData(gen);
      ASSERT_TRUE(file.write(data));
    }
  }

  return;
}

class Filesystem : public ::testing::Test {
public:
  fs::path testWorkspace;
  LTree tree;

  void SetUp() override {
    testWorkspace = mkdtemp(std::string("/tmp/FSTests_XXXXXX").data());

    ASSERT_NO_FATAL_FAILURE(generateRandomDirectory(testWorkspace.string()));

    Tree tree = Tree(testWorkspace.string(), true);
    ASSERT_TRUE(tree.writeMerkleTree());

    const auto current = Node::getHeadFile();
    ASSERT_TRUE(current.has_value() && current.value().length() == 64);
    alignas(32) std::array<char, 64> arr;
    memcpy(arr.data(), current.value().data(), 64);
    this->tree = LTree(arr, "/FreeSync", true);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(testWorkspace, ec);
    unlink(testWorkspace.string().data());
  }
};
TEST_F(Filesystem, generateMerkleTree) {
  for (const auto &e : fs::recursive_directory_iterator(this->testWorkspace)) {
    if (!e.is_regular_file() || e.file_size() == 0)
      continue;

    std::string path = "/FreeSync/";
    path += fs::relative(e.path(), this->testWorkspace);

    // std::cout << "p:" << e.path() << '\n';
    // std::cout << "info:" << fs::path(path).parent_path().string() << '\n';
    //
    // auto info =
    //     this->tree.getChildTree(fs::path(path).parent_path().string()).value();
    // for (const auto &ch : info.getChildren())
    //   std::cout << ch._fileName << '\n';

    auto child = tree.getChildFile(path);
    ASSERT_TRUE(child.has_value()) << path;
    DiskView view(e.path().string());
    ASSERT_TRUE(view);
    ASSERT_EQ(view.toSpan().size(), child->toSpan().size());
    ASSERT_GT(view.toSpan().size(), 0);
    ASSERT_TRUE(!memcmp(child->toSpan().data(), view.toSpan().data(),
                        view.toSpan().size()));
  }
}
