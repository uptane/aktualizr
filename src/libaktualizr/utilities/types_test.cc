#include <gtest/gtest.h>

#include "libaktualizr/types.h"

TimeStamp now("2017-01-01T01:00:00Z");

/* Parse Uptane timestamps. */
TEST(Types, TimeStampParsing) {
  TimeStamp t_old("2038-01-19T02:00:00Z");
  TimeStamp t_new("2038-01-19T03:14:06Z");

  TimeStamp t_invalid;

  EXPECT_LT(t_old, t_new);
  EXPECT_GT(t_new, t_old);
  EXPECT_FALSE(t_invalid < t_old);
  EXPECT_FALSE(t_old < t_invalid);
  EXPECT_FALSE(t_invalid < t_invalid);
}

/* Throw an exception if an Uptane timestamp is invalid. */
TEST(Types, TimeStampParsingInvalid) { EXPECT_THROW(TimeStamp("2038-01-19T0"), TimeStamp::InvalidTimeStamp); }

/* Get current time. */
TEST(Types, TimeStampNow) {
  TimeStamp t_past("1982-12-13T02:00:00Z");
  TimeStamp t_future("2038-01-19T03:14:06Z");
  TimeStamp t_now(TimeStamp::Now());

  EXPECT_LT(t_past, t_now);
  EXPECT_LT(t_now, t_future);
}

TEST(Types, ResultCode) {
  data::ResultCode ok_res{data::ResultCode::Numeric::kOk};
  EXPECT_EQ(ok_res.num_code, data::ResultCode::Numeric::kOk);
  EXPECT_EQ(ok_res.ToString(), "OK");
  std::string repr = ok_res.toRepr();
  EXPECT_EQ(repr, "\"OK\":0");
  EXPECT_EQ(data::ResultCode::fromRepr(repr), ok_res);

  // legacy format
  EXPECT_EQ(data::ResultCode::fromRepr("OK:0"), ok_res);

  // !
  EXPECT_NE(ok_res, data::ResultCode(data::ResultCode::Numeric::kOk, "OK2"));
  EXPECT_NE(ok_res, data::ResultCode(data::ResultCode::Numeric::kGeneralError, "OK"));
  EXPECT_EQ(data::ResultCode::fromRepr("OK"), data::ResultCode(data::ResultCode::Numeric::kUnknown, "OK"));
}

TEST(Types, MergeJsonSingleLevel) {
  Json::Value value1;
  value1["a"] = "aaa";
  value1["b"] = "bbb";
  value1["c"] = 333;
  value1["d"] = Json::Value(Json::arrayValue);
  value1["e"] = 1.2345;
  value1["f"] = true;
  value1["g"] = Json::Value(Json::nullValue);

  Json::Value value2;
  value2["b"] = 4444;
  value2["c"] = Json::Value(Json::arrayValue);
  value2["d"] = 23.45;
  value2["e"] = false;
  value2["f"] = Json::Value(Json::nullValue);
  value2["g"] = "gggg";
  value2["h"] = Json::Value(Json::nullValue);
  value2["i"] = 678;

  Json::Value res1 = utils::MergeJson(value1, value2);
  EXPECT_EQ(res1["a"], "aaa");
  EXPECT_EQ(res1["b"], "bbb");
  EXPECT_EQ(res1["c"], 333);
  EXPECT_EQ(res1["d"].type(), Json::arrayValue);
  EXPECT_EQ(res1["d"].size(), 0);
  EXPECT_EQ(res1["e"], 1.2345);
  EXPECT_EQ(res1["f"], true);
  EXPECT_EQ(res1["g"], "gggg");
  EXPECT_EQ(res1["h"].type(), Json::nullValue);
  EXPECT_EQ(res1["i"], 678);

  Json::Value res2 = utils::MergeJson(value2, value1);
  EXPECT_EQ(res2["a"], "aaa");
  EXPECT_EQ(res2["b"], 4444);
  EXPECT_EQ(res2["c"].type(), Json::arrayValue);
  EXPECT_EQ(res2["c"].size(), 0);
  EXPECT_EQ(res2["d"], 23.45);
  EXPECT_EQ(res2["e"], false);
  EXPECT_EQ(res2["f"], true);
  EXPECT_EQ(res2["g"], "gggg");
  EXPECT_EQ(res2["h"].type(), Json::nullValue);
  EXPECT_EQ(res2["i"], 678);
}

TEST(Types, MergeJsonMultiLevel) {
  Json::Value value1;
  value1["a"] = "aaa-from-value1";
  value1["b"] = 333;
  value1["c"]["cc1"] = "c1-from-value1";
  value1["c"]["cc2"] = 222;
  value1["c"]["cc3"]["ccc1"] = "ccc1-from-value1";
  value1["c"]["cc3"]["ccc2"] = 0;
  value1["c"]["cc3"]["ccc3"] = Json::Value(Json::nullValue);
  value1["c"]["cc3"]["ccc4"] = Json::Value(Json::nullValue);

  Json::Value empty(Json::nullValue);
  Json::Value res0a = utils::MergeJson(value1, empty);
  EXPECT_EQ(res0a["a"], "aaa-from-value1");
  EXPECT_EQ(res0a["b"], 333);
  EXPECT_EQ(res0a["c"]["cc1"], "c1-from-value1");
  EXPECT_EQ(res0a["c"]["cc2"], 222);
  EXPECT_EQ(res0a["c"]["cc3"]["ccc1"], "ccc1-from-value1");
  EXPECT_EQ(res0a["c"]["cc3"]["ccc2"], 0);
  EXPECT_EQ(res0a["c"]["cc3"]["ccc3"].type(), Json::nullValue);
  EXPECT_EQ(res0a["c"]["cc3"]["ccc4"].type(), Json::nullValue);

  Json::Value res0b = utils::MergeJson(empty, value1);
  EXPECT_EQ(res0b["a"], "aaa-from-value1");
  EXPECT_EQ(res0b["b"], 333);
  EXPECT_EQ(res0b["c"]["cc1"], "c1-from-value1");
  EXPECT_EQ(res0b["c"]["cc2"], 222);
  EXPECT_EQ(res0b["c"]["cc3"]["ccc1"], "ccc1-from-value1");
  EXPECT_EQ(res0b["c"]["cc3"]["ccc2"], 0);
  EXPECT_EQ(res0b["c"]["cc3"]["ccc3"].type(), Json::nullValue);
  EXPECT_EQ(res0b["c"]["cc3"]["ccc4"].type(), Json::nullValue);

  // Override all top-level properties:
  Json::Value value2;
  value2["a"] = "aaa-from-value2";
  value2["b"] = -333;
  value2["c"] = "c1-from-value2";
  value2["d"] = "d1-from-value2";

  Json::Value res1 = utils::MergeJson(value1, value2);
  EXPECT_EQ(res1["a"], "aaa-from-value1");
  EXPECT_EQ(res1["b"], 333);
  EXPECT_EQ(res1["c"]["cc1"], "c1-from-value1");
  EXPECT_EQ(res1["c"]["cc2"], 222);
  EXPECT_EQ(res1["c"]["cc3"]["ccc1"], "ccc1-from-value1");
  EXPECT_EQ(res1["c"]["cc3"]["ccc2"], 0);
  EXPECT_EQ(res1["c"]["cc3"]["ccc3"].type(), Json::nullValue);
  EXPECT_EQ(res1["c"]["cc3"]["ccc4"].type(), Json::nullValue);
  EXPECT_EQ(res1["d"], "d1-from-value2");

  Json::Value res2 = utils::MergeJson(value2, value1);
  EXPECT_EQ(res2["a"], "aaa-from-value2");
  EXPECT_EQ(res2["b"], -333);
  EXPECT_EQ(res2["c"], "c1-from-value2");
  EXPECT_EQ(res2["d"], "d1-from-value2");

  Json::Value value3;
  value3["c"]["cc1"] = "c1-from-value3";
  value3["c"]["cc2"] = Json::Value(Json::nullValue);
  value3["c"]["cc3"]["ccc1"] = "ccc1-from-value3";
  value3["c"]["cc3"]["ccc2"] = Json::Value(Json::nullValue);
  value3["c"]["cc3"]["ccc3"]["cccc1"] = "";
  value3["c"]["cc3"]["ccc3"]["cccc2"] = 2222;
  value3["c"]["cc3"]["ccc3"]["cccc4"] = "";

  Json::Value res3 = utils::MergeJson(value1, value3);
  EXPECT_EQ(res3["a"], "aaa-from-value1");
  EXPECT_EQ(res3["b"], 333);
  EXPECT_EQ(res3["c"]["cc1"], "c1-from-value1");
  EXPECT_EQ(res3["c"]["cc2"], 222);
  EXPECT_EQ(res3["c"]["cc3"]["ccc1"], "ccc1-from-value1");
  EXPECT_EQ(res3["c"]["cc3"]["ccc2"], 0);
  EXPECT_EQ(res3["c"]["cc3"]["ccc3"].type(), Json::objectValue);
  EXPECT_EQ(res3["c"]["cc3"]["ccc3"]["cccc1"], "");
  EXPECT_EQ(res3["c"]["cc3"]["ccc3"]["cccc2"], 2222);
  EXPECT_EQ(res3["c"]["cc3"]["ccc3"]["cccc4"], "");

  Json::Value res4 = utils::MergeJson(value3, value1);
  EXPECT_EQ(res4["a"], "aaa-from-value1");
  EXPECT_EQ(res4["b"], 333);
  EXPECT_EQ(res4["c"]["cc1"], "c1-from-value3");
  EXPECT_EQ(res4["c"]["cc2"], 222);
  EXPECT_EQ(res4["c"]["cc3"]["ccc1"], "ccc1-from-value3");
  EXPECT_EQ(res4["c"]["cc3"]["ccc2"], 0);
  EXPECT_EQ(res4["c"]["cc3"]["ccc3"].type(), Json::objectValue);
  EXPECT_EQ(res4["c"]["cc3"]["ccc3"]["cccc1"], "");
  EXPECT_EQ(res4["c"]["cc3"]["ccc3"]["cccc2"], 2222);
  EXPECT_EQ(res4["c"]["cc3"]["ccc3"]["cccc3"].type(), Json::nullValue);
  EXPECT_EQ(res4["c"]["cc3"]["ccc3"]["cccc4"], "");

  //  std::cout << "res1: " << res1 << std::endl;
  //  std::cout << "res2: " << res2 << std::endl;
  //  std::cout << "res3: " << res3 << std::endl;
  //  std::cout << "res4: " << res4 << std::endl;
}

TEST(Types, MergeJsonRealCaseNoIgnore) {
  Json::Value value1;
  value1["ecuIdentifiers"]["ed9d697502"]["hardwareId"] = "bootloader";
  value1["uri"] = Json::Value(Json::nullValue);

  Json::Value value2;
  value2["name"] = "package-name";
  value2["version"] = "V1";
  value2["hardwareIds"] = Json::Value(Json::arrayValue);
  value2["hardwareIds"].append("bootloader");
  value2["targetFormat"] = "BINARY";
  value2["uri"] = "http://site.com/path/";
  value2["createdAt"] = "2022-06-22T14:52:11Z";
  value2["updatedAt"] = "2022-06-22T15:09:09Z";

  Json::Value res1 = utils::MergeJson(value1, value2);
  EXPECT_EQ(res1["ecuIdentifiers"]["ed9d697502"]["hardwareId"], "bootloader");
  EXPECT_EQ(res1["uri"], "http://site.com/path/");
  EXPECT_EQ(res1["name"], "package-name");
  EXPECT_EQ(res1["version"], "V1");
  EXPECT_EQ(res1["hardwareIds"][0], "bootloader");
  EXPECT_EQ(res1["targetFormat"], "BINARY");
  EXPECT_EQ(res1["uri"], "http://site.com/path/");
  EXPECT_EQ(res1["createdAt"], "2022-06-22T14:52:11Z");
  EXPECT_EQ(res1["updatedAt"], "2022-06-22T15:09:09Z");

  Json::Value res2 = utils::MergeJson(value2, value1);
  EXPECT_EQ(res2["ecuIdentifiers"]["ed9d697502"]["hardwareId"], "bootloader");
  EXPECT_EQ(res2["uri"], "http://site.com/path/");
  EXPECT_EQ(res2["name"], "package-name");
  EXPECT_EQ(res2["version"], "V1");
  EXPECT_EQ(res2["hardwareIds"][0], "bootloader");
  EXPECT_EQ(res2["targetFormat"], "BINARY");
  EXPECT_EQ(res2["uri"], "http://site.com/path/");
  EXPECT_EQ(res2["createdAt"], "2022-06-22T14:52:11Z");
  EXPECT_EQ(res2["updatedAt"], "2022-06-22T15:09:09Z");
}

TEST(Types, MergeJsonRealCaseIgnore) {
  Json::Value value1;
  value1["ecuIdentifiers"]["ed9d697502"]["hardwareId"] = "bootloader";
  value1["uri"] = "http://from-director/";

  Json::Value value2;
  value2["name"] = "package-name";
  value2["version"] = "V1";
  value2["hardwareIds"] = Json::Value(Json::arrayValue);
  value2["hardwareIds"].append("bootloader");
  value2["targetFormat"] = "BINARY";
  value2["uri"] = "http://site.com/path/";
  value2["createdAt"] = "2022-06-22T14:52:11Z";
  value2["updatedAt"] = "2022-06-22T15:09:09Z";

  std::vector<std::string> ignore1 = {"hardwareIds", "targetFormat", "uri"};
  Json::Value res1 = utils::MergeJson(value1, value2, &ignore1);
  EXPECT_EQ(res1["ecuIdentifiers"]["ed9d697502"]["hardwareId"], "bootloader");
  EXPECT_EQ(res1["uri"], "http://from-director/");
  EXPECT_EQ(res1["name"], "package-name");
  EXPECT_EQ(res1["version"], "V1");
  EXPECT_EQ(res1["hardwareIds"].type(), Json::nullValue);
  EXPECT_EQ(res1["targetFormat"].type(), Json::nullValue);
  EXPECT_EQ(res1["createdAt"], "2022-06-22T14:52:11Z");
  EXPECT_EQ(res1["updatedAt"], "2022-06-22T15:09:09Z");
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#endif
