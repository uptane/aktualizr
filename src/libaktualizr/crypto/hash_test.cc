#include <gtest/gtest.h>

#include "crypto.h"

#include <fstream>

#include "logging/logging.h"
#include "utilities/utils.h"

TEST(Hash, EncodeDecode) {
  std::vector<Hash> hashes = {{Hash::Type::kSha256, "abcd"}, {Hash::Type::kSha512, "defg"}};

  std::string encoded = Hash::encodeVector(hashes);
  std::vector<Hash> decoded = Hash::decodeVector(encoded);

  EXPECT_EQ(hashes, decoded);
}

TEST(Hash, DecodeBad) {
  std::string bad1 = ":";
  EXPECT_EQ(Hash::decodeVector(bad1), std::vector<Hash>{});

  std::string bad2 = ":abcd;sha256:12";
  EXPECT_EQ(Hash::decodeVector(bad2), std::vector<Hash>{Hash(Hash::Type::kSha256, "12")});

  std::string bad3 = "sha256;";
  EXPECT_EQ(Hash::decodeVector(bad3), std::vector<Hash>{});

  std::string bad4 = "sha256:;";
  EXPECT_EQ(Hash::decodeVector(bad4), std::vector<Hash>{});
}

TEST(Hash, shortTag) {
  std::vector<Hash> hashes = {{Hash::Type::kSha256, "B5bB9d8014a0f9b1d61e21e796d78dccdf1352f23cd32812f4850b878ae4944c"},
                              {Hash::Type::kSha512,
                               "0cf9180a764aba863a67b6d72f0918bc131c6772642cb2dce5a34f0a702f9470ddc2bf125c12198b1995c23"
                               "3c34b4afd346c54a2334c350a948a51b6e8b4e6b6"}};

  EXPECT_EQ(Hash::shortTag(hashes), "b5bb9d8014a0");
  std::reverse(hashes.begin(), hashes.end());
  EXPECT_EQ(Hash::shortTag(hashes), "b5bb9d8014a0");
  std::vector<Hash> const one = {{Hash::Type::kSha512,
                                  "0cf9180a764aba863a67b6d72f0918bc131c6772642cb2dce5a34f0a702f9470ddc2bf125c12198b1995"
                                  "c233c34b4afd346c54a2334c350a948a51b6e8b4e6b6"}};

  EXPECT_EQ(Hash::shortTag(one), "0cf9180a764a");
  std::vector<Hash> const small = {{Hash::Type::kSha256, "small"}};
  EXPECT_EQ(Hash::shortTag(small), "small");
}

TEST(Hash, Generate) {
  TemporaryFile file;
  std::string contents = "foobar";
  file.PutContents(contents);

  auto direct = Hash::generate(Hash::Type::kSha256, contents);
  std::ifstream input_stream(file.PathString());
  ssize_t len;
  auto via_file = Hash::generate(Hash::Type::kSha256, input_stream, &len);

  EXPECT_EQ(len, contents.size());
  EXPECT_EQ(direct, via_file);
}

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  logger_set_threshold(boost::log::trivial::trace);
  return RUN_ALL_TESTS();
}
#endif
