// Behavior tests for json.h: parser + serializer behaviors that the rest of
// the codebase relies on. toolcall's cases never touch parse/dump, so this
// file closes that coverage gap while pinning down the API behaviors.
#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <type_traits>

#include "support/json.h"

namespace {

std::string Dump(const Json& j) { return j.dump(); }

}  // namespace

TEST(JsonTest, ConstructAndDump) {
  Json obj = Json::object{{"file_path", "/a/b.txt"},
                          {"n", Json(7)},
                          {"ok", Json(true)},
                          {"z", Json(nullptr)}};
  EXPECT_TRUE(obj.is_object());
  // std::map guarantees key order, so dump is deterministic.
  EXPECT_EQ(Dump(obj),
            "{\"file_path\":\"/a/b.txt\",\"n\":7,\"ok\":true,\"z\":null}");
}

TEST(JsonTest, TypePredicatesAndAccessors) {
  Json obj = Json::object{{"s", Json("x")}, {"n", Json(3)}, {"b", Json(true)}};
  EXPECT_TRUE(obj.at("s").is_string());
  EXPECT_TRUE(obj.at("n").is_number());
  EXPECT_TRUE(obj.at("b").is_bool());
  EXPECT_TRUE(obj.at("missing").is_null());
  EXPECT_EQ(obj.at("s").get<std::string>(), "x");
  EXPECT_EQ(obj.get_int("n"), 3);
  EXPECT_TRUE(obj.get_bool("b"));
  // Type mismatch -> fallback, never throws.
  EXPECT_EQ(obj.get_string("n"), "");
  EXPECT_EQ(obj.get_int("s", -1), -1);
}

TEST(JsonTest, Numbers) {
  EXPECT_EQ(Dump(Json(1.0)), "1");      // integral values lose the trailing .0
  EXPECT_EQ(Dump(Json(1.5)), "1.5");
  EXPECT_EQ(Dump(Json(-3)), "-3");
  EXPECT_EQ(Json(1), Json(1.0));        // numeric equality
  EXPECT_NE(Json("a"), Json(1));        // cross-type inequality
}

TEST(JsonTest, ParseRoundTrip) {
  const char* src =
      "{\"a\":[1,2.5,-3e2,true,false,null,\"s\"],\"b\":{\"c\":\"d\"}}";
  std::string err;
  Json p = Json::parse(src, err);
  EXPECT_TRUE(err.empty()) << err;
  EXPECT_TRUE(p.is_object());
  EXPECT_TRUE(p.at("a").is_array());
  EXPECT_EQ(p.at("a").size(), 7u);
  EXPECT_DOUBLE_EQ(p.at("a")[std::size_t{2}].as_number(), -300.0);
  EXPECT_EQ(p.at("b").at("c").get<std::string>(), "d");
  // Round-trip stability: dump(parse(dump(x))) == dump(x).
  std::string err2;
  EXPECT_EQ(Json::parse(p.dump(), err2).dump(), p.dump());
  EXPECT_TRUE(err2.empty()) << err2;
}

TEST(JsonTest, DumpEscapes) {
  EXPECT_EQ(Dump(Json("a\nb\t\"c\"")), "\"a\\nb\\t\\\"c\\\"\"");
  EXPECT_EQ(Dump(Json(std::string("\x01", 1))), "\"\\u0001\"");  // control character
}

TEST(JsonTest, ParseEscapes) {
  std::string err;
  Json esc = Json::parse("\"q\\\"b\\\\s\\/n\\b\\f\\r\\t\"", err);
  EXPECT_TRUE(err.empty()) << err;
  EXPECT_EQ(esc.as_string(), "q\"b\\s/n\b\f\r\t");
}

TEST(JsonTest, ParseUnicodeEscapes) {
  std::string err;
  Json uni = Json::parse("\"\\u4e2d\\u6587 \\u00e9\"", err);
  EXPECT_TRUE(err.empty()) << err;
  EXPECT_EQ(uni.as_string(), "\u4e2d\u6587 \u00e9");

  Json sur = Json::parse("\"\\ud83d\\ude00\"", err);  // U+1F600
  EXPECT_TRUE(err.empty()) << err;
  EXPECT_EQ(sur.as_string(), "\U0001F600");
}

TEST(JsonTest, ParseRejectsBadInput) {
  const char* bad[] = {
      "",             "{",           "[1,",        "{\"a\"}",      "nul",
      "truex",        "01",          "1.",         "1e",           "\"unterminated",
      "\"bad\\q\"",   "\"\\ud83d\"", "{\"a\":1,}", "[1 2]",        "{'a':1}",
      "{\"a\":1}x",
  };
  for (const char* b : bad) {
    std::string err;
    Json r = Json::parse(b, err);
    EXPECT_FALSE(err.empty()) << "should reject: " << b;
    EXPECT_TRUE(r.is_null()) << "should be null: " << b;
  }
}

TEST(JsonTest, DeepNestingRejectedWithoutCrash) {
  std::string deep;
  for (int i = 0; i < 500; ++i) deep += "[";
  std::string err;
  Json d = Json::parse(deep, err);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(d.is_null());
}

TEST(JsonTest, ItemsIteration) {
  Json obj = Json::object{{"a", Json(1)}, {"b", Json(2)}, {"c", Json(3)}};
  int n = 0;
  for (const auto& [k, v] : obj.items()) {
    EXPECT_FALSE(k.empty());
    EXPECT_TRUE(v.is_number());
    ++n;
  }
  EXPECT_EQ(n, 3);
  // Iterating a non-object yields an empty range, no crash.
  n = 0;
  for (const auto& [k, v] : Json(5).items()) {
    (void)k;
    (void)v;
    ++n;
  }
  EXPECT_EQ(n, 0);
}

TEST(JsonTest, MutatorsAndArrays) {
  Json m = Json::object{{"x", Json(1)}};
  EXPECT_TRUE(m.has("x"));
  EXPECT_TRUE(m.erase("x"));
  EXPECT_FALSE(m.has("x"));
  EXPECT_FALSE(m.erase("x"));

  Json arr = Json::array();
  EXPECT_TRUE(arr.is_array());
  arr.push_back(Json(1));
  arr.push_back(Json("two"));
  EXPECT_EQ(arr.size(), 2u);
  EXPECT_EQ(Dump(arr), "[1,\"two\"]");
}

TEST(JsonTest, WriteAccessors) {
  Json j;
  j["a"] = Json(1);                 // a non-object converts in place to an empty object
  EXPECT_TRUE(j.is_object());
  EXPECT_EQ(j.at("a").as_int(), 1);
  j["nested"]["deep"] = Json("v");
  EXPECT_EQ(j.at("nested").at("deep").get<std::string>(), "v");
  // operator[](key) also builds objects.
  Json k = Json::object();
  k["x"] = Json(true);
  EXPECT_TRUE(k.get_bool("x"));
}

// ---- Added behaviors (find/end, alias, non-finite numbers) ------------------

// Call sites write j.find("session_id"), compare the iterator with
// j.end(), and reads values via it->second. find returns a std::map iterator
// (pointing at a pair); a miss equals any instance's end() (unified sentinel).
TEST(JsonTest, FindAndEnd) {
  Json obj = Json::object{{"session_id", Json("abc")},
                          {"items", Json::array_of({Json(1), Json(2)})}};
  auto it = obj.find("items");
  ASSERT_NE(it, obj.end());
  EXPECT_TRUE(it->second.is_array());
  EXPECT_EQ(it->second.size(), 2u);

  auto s = obj.find("session_id");
  ASSERT_NE(s, obj.end());
  EXPECT_TRUE(s->second.is_string());
  EXPECT_EQ(s->second.get<std::string>(), "abc");

  EXPECT_EQ(obj.find("nope"), obj.end());
  // find on a non-object always yields end(), no crash.
  EXPECT_EQ(Json(5).find("x"), Json(5).end());
  // Empty object: a find miss equals any Json instance's end() (unified
  // sentinel, comparable across instances).
  Json empty = Json::object();
  EXPECT_EQ(empty.find("x"), empty.end());
  EXPECT_EQ(empty.find("x"), empty.end());
  // Note: Json::object() is value-initialization of the map type alias, so
  // .find lands on std::map, not Json::find — a temporary map can't be
  // compared against a Json sentinel here.
  Json other = Json::object();
  EXPECT_EQ(empty.find("x"), other.end());
}

// Call sites write support::Json throughout; the alias must be usable.
TEST(JsonTest, SupportNamespaceAlias) {
  support::Json j = support::Json::object{{"a", support::Json(1)}};
  EXPECT_TRUE(j.is_object());
  EXPECT_EQ(j.get_int("a"), 1);
  std::string err;
  support::Json p = support::Json::parse("{\"a\":1}", err);
  EXPECT_TRUE(err.empty()) << err;
  EXPECT_EQ(p.dump(), "{\"a\":1}");
  static_assert(std::is_same_v<support::Json, Json>,
                "support::Json must alias ::Json");
}

// Non-finite numbers (NaN/Inf) are not valid JSON: dump writes null, parse rejects.
TEST(JsonTest, NonFiniteNumbers) {
  const double kInf = std::numeric_limits<double>::infinity();
  const double kNaN = std::numeric_limits<double>::quiet_NaN();

  EXPECT_EQ(Dump(Json(kInf)), "null");
  EXPECT_EQ(Dump(Json(-kInf)), "null");
  EXPECT_EQ(Dump(Json(kNaN)), "null");
  // Same when nested in objects/arrays.
  EXPECT_EQ(Dump(Json::object{{"x", Json(kInf)}}), "{\"x\":null}");
  EXPECT_EQ(Dump(Json::array_of({Json(kNaN)})), "[null]");
  // A string parsed out of NaN/Inf ("1e999" overflowing to inf) must also fall
  // back to null.
  std::string err;
  Json p = Json::parse("\"1e999\"", err);
  EXPECT_TRUE(err.empty()) << err;  // it is a string, not a number

  // Upstreams can hand over overflow literals like 1e999: they must be treated
  // as errors, never silently become inf.
  std::string e2;
  Json over = Json::parse("1e999", e2);
  EXPECT_FALSE(e2.empty());
  EXPECT_TRUE(over.is_null());
}
