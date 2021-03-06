#include <gtest/gtest.h>

#include "ostree_dir_repo.h"
#include "ostree_ref.h"

/* Reject invalid path. */
TEST(dir_repo, invalid_path) {
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>("invalid_path");
  EXPECT_FALSE(src_repo->LooksValid());
}

/* Reject invalid repo configuration. */
TEST(dir_repo, invalid_config) {
  TemporaryDirectory temp_dir;
  Utils::copyDir("tests/sota_tools/repo", temp_dir.Path());
  Utils::writeFile(temp_dir.Path() / "repo/config", std::string("123"));
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>(temp_dir.Path() / "repo");
  EXPECT_FALSE(src_repo->LooksValid());
}

/* Reject invalid repo configuration. */
TEST(dir_repo, wrong_ini) {
  TemporaryDirectory temp_dir;
  Utils::copyDir("tests/sota_tools/repo", temp_dir.Path());
  Utils::writeFile(temp_dir.Path() / "repo/config", std::string("[core]"));
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>(temp_dir.Path() / "repo");
  EXPECT_FALSE(src_repo->LooksValid());
}

/* Reject bare mode repo. */
TEST(dir_repo, bare_mode) {
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>("tests/sota_tools/bare-repo");
  EXPECT_FALSE(src_repo->LooksValid());
}

/* Verify a local OSTree repository. */
TEST(dir_repo, good_repo) {
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>("tests/sota_tools/repo");
  EXPECT_TRUE(src_repo->LooksValid());
}

/* Find OSTree ref in local repository. */
TEST(dir_repo, getRef) {
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>("tests/sota_tools/repo");
  EXPECT_EQ(src_repo->GetRef("master").GetHash().string(),
            std::string("16ef2f2629dc9263fdf3c0f032563a2d757623bbc11cf99df25c3c3f258dccbe"));
}

TEST(dir_repo, root) {
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>("tests/sota_tools/repo");
  EXPECT_EQ(src_repo->root(), std::string("tests/sota_tools/repo"));
}

/* Find OSTree object in local repository.
 * Check all valid OSTree object extensions. */
TEST(dir_repo, GetObject) {
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>("tests/sota_tools/repo");
  const uint8_t hash[32] = {0x2a, 0x28, 0xda, 0xc4, 0x2b, 0x76, 0xc2, 0x01, 0x5e, 0xe3, 0xc4,
                            0x1c, 0xc4, 0x18, 0x3b, 0xb8, 0xb5, 0xc7, 0x90, 0xfd, 0x21, 0xfa,
                            0x5c, 0xfa, 0x08, 0x02, 0xc6, 0xe1, 0x1f, 0xd0, 0xed, 0xbe};
  auto object = src_repo->GetObject(hash, OstreeObjectType::OSTREE_OBJECT_TYPE_DIR_META);
  std::stringstream s;
  s << object;
  EXPECT_EQ(s.str(), std::string("2a/28dac42b76c2015ee3c41cc4183bb8b5c790fd21fa5cfa0802c6e11fd0edbe.dirmeta"));
}

/* Abort if OSTree object is not found. */
TEST(dir_repo, GetObject_Missing) {
  OSTreeRepo::ptr src_repo = std::make_shared<OSTreeDirRepo>("tests/sota_tools/repo");
  const uint8_t hash[32] = {0x01, 0x28, 0xda, 0xc4, 0x2b, 0x76, 0xc2, 0x01, 0x5e, 0xe3, 0xc4,
                            0x1c, 0xc4, 0x18, 0x3b, 0xb8, 0xb5, 0xc7, 0x90, 0xfd, 0x21, 0xfa,
                            0x5c, 0xfa, 0x08, 0x02, 0xc6, 0xe1, 0x1f, 0xd0, 0xed, 0xbe};
  EXPECT_THROW(src_repo->GetObject(hash, OstreeObjectType::OSTREE_OBJECT_TYPE_DIR_META), OSTreeObjectMissing);
}

TEST(dir_repo, GetPathForHash) {
  auto p = OSTreeDirRepo::GetPathForHash(
      OSTreeHash::Parse("1f3378927c2d062e40a372414c920219e506afeb8ef25f9ff72a27b792cd093a"),
      OstreeObjectType::OSTREE_OBJECT_TYPE_COMMIT);
  boost::filesystem::path golden{"1f/3378927c2d062e40a372414c920219e506afeb8ef25f9ff72a27b792cd093a.commit"};
  EXPECT_EQ(p, golden);
}

TEST(dir_repo, GetPathForHashExtensions) {
  auto hash = OSTreeHash::Parse("1f3378927c2d062e40a372414c920219e506afeb8ef25f9ff72a27b792cd093a");
  auto p = OSTreeDirRepo::GetPathForHash(hash, OstreeObjectType::OSTREE_OBJECT_TYPE_FILE);
  EXPECT_EQ(p.extension(), ".filez");
  p = OSTreeDirRepo::GetPathForHash(hash, OstreeObjectType::OSTREE_OBJECT_TYPE_DIR_TREE);
  EXPECT_EQ(p.extension(), ".dirtree");
  p = OSTreeDirRepo::GetPathForHash(hash, OstreeObjectType::OSTREE_OBJECT_TYPE_DIR_META);
  EXPECT_EQ(p.extension(), ".dirmeta");
  p = OSTreeDirRepo::GetPathForHash(hash, OstreeObjectType::OSTREE_OBJECT_TYPE_COMMIT);
  EXPECT_EQ(p.extension(), ".commit");
  EXPECT_ANY_THROW(OSTreeDirRepo::GetPathForHash(hash, OSTREE_OBJECT_TYPE_UNKNOWN));
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#endif

// vim: set tabstop=2 shiftwidth=2 expandtab:
