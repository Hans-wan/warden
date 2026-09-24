// Bundled minimal JSON (object/string/number/bool/array/null), an RFC 8259 subset.
// Constraint: exceptions are banned — all failures surface through the err
// out-param or empty-value returns; nothing ever throws.
//
//   Json j = Json::object{{"file_path", "/a/b.txt"}};   // construction
//   Json v = Json(120000);                              // number
//   Json p = Json::parse(text, err);                    // parse (non-empty err means failure)
//   std::string s = p.dump();                           // serialize
//   p.at("a"); p["a"]; for (auto& [k, v] : p.items())   // access
//
// Value semantics (std::variant); copy/move/compare are all compiler-generated.
// Note: Json::object / Json::array are **type aliases** of std::map /
// std::vector (not functions), so both Json::object() and
// Json::object{{k, v}} are valid spellings.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

class Json {
 public:
  using Object = std::map<std::string, Json>;
  using Array = std::vector<Json>;
  using object = Object;
  using array = Array;

  Json() : v_(nullptr) {}
  Json(std::nullptr_t) : v_(nullptr) {}
  Json(bool b) : v_(b) {}
  Json(const char* s) : v_(std::string(s ? s : "")) {}
  Json(std::string s) : v_(std::move(s)) {}
  template <class T, std::enable_if_t<std::is_arithmetic_v<T> &&
                                          !std::is_same_v<T, bool>, int> = 0>
  Json(T n) : v_(static_cast<double>(n)) {}
  Json(Object o) : v_(std::move(o)) {}
  Json(Array a) : v_(std::move(a)) {}

  // Parses. On success err is empty; on failure returns null and writes err.
  static Json parse(std::string_view text, std::string& err);
  static Json parse(std::string_view text) {
    std::string err;
    return parse(text, err);
  }

  // Serializes (compact, no extra whitespace; stable key order, since the
  // underlying container is a std::map).
  std::string dump() const;

  bool is_null() const { return v_.index() == kNull; }
  bool is_bool() const { return v_.index() == kBool; }
  bool is_number() const { return v_.index() == kNumber; }
  bool is_string() const { return v_.index() == kString; }
  bool is_array() const { return v_.index() == kArray; }
  bool is_object() const { return v_.index() == kObject; }

  // Scalar access; a type mismatch returns the fallback (never throws).
  bool as_bool(bool fallback = false) const;
  double as_number(double fallback = 0.0) const;
  std::string as_string(std::string fallback = "") const;
  // Integer read (truncating). JSON numbers are stored as double uniformly,
  // hence the explicit conversion.
  long as_int(long fallback = 0) const;

  // Uniform access entry for any type (used by Tasks 7-12).
  template <class T>
  T get() const;
  template <class T>
  T get(std::string_view key) const;

  // Erases a key from the map; no-op on non-objects. Returns whether it erased.
  bool erase(std::string_view key);
  // The key exists and its value is not null.
  bool has(std::string_view key) const;

  // --- Object access -------------------------------------------------------
  // Read access: a missing key or non-object returns a static null reference.
  const Json& at(std::string_view key) const;
  // Write access: a non-object is converted in place to an empty object before
  // insertion; the returned reference can be assigned through.
  Json& operator[](std::string_view key);
  const Json& operator[](std::string_view key) const { return at(key); }

  // Object lookup (used by hook-payload call sites).
  //   auto it = j.find("session_id");
  //   if (it != j.end() && it->second.is_string()) ...
  // find returns a std::map iterator (pointing at pair<const string, Json>);
  // read values via it->second. A miss equals j.end() and the end() of any
  // other Json instance (a single unified sentinel). On non-objects, find
  // always yields end().
  using const_iterator = Object::const_iterator;
  const_iterator find(std::string_view key) const;
  const_iterator end() const;

  // Convenience read: returns the value only when present and well-typed,
  // otherwise the fallback.
  std::string get_string(std::string_view key, std::string fallback = "") const;
  double get_number(std::string_view key, double fallback = 0.0) const;
  long get_int(std::string_view key, long fallback = 0) const;
  bool get_bool(std::string_view key, bool fallback = false) const;

  // Object iteration. Non-objects yield an empty range, safe in range-for.
  using Item = std::pair<const std::string&, const Json&>;
  class ItemRange {
   public:
    explicit ItemRange(const Json* self) : self_(self) {}
    class iterator {
     public:
      using inner = Object::const_iterator;
      explicit iterator(inner it) : it_(it) {}
      bool operator!=(const iterator& o) const { return it_ != o.it_; }
      iterator& operator++() {
        ++it_;
        return *this;
      }
      Item operator*() const { return Item(it_->first, it_->second); }

     private:
      inner it_;
    };
    iterator begin() const { return iterator(obj().begin()); }
    iterator end() const { return iterator(obj().end()); }

   private:
    const Object& obj() const;
    const Json* self_;
  };
  ItemRange items() const { return ItemRange(this); }

  // --- Array access --------------------------------------------------------
  // Element count. Meaningful only for array (element count), object (key
  // count), and string (byte count); null / bool / number always return 0.
  // **Never use size() == 0 to test for absence** — use is_null() or
  // has(key) for that.
  std::size_t size() const;
  const Json& operator[](std::size_t index) const;
  Json& operator[](std::size_t index);
  static Json array_of(Array a) { return Json(std::move(a)); }
  // Appends to the array; non-arrays are converted in place to an empty array.
  void push_back(Json v);

  // Ordered comparison: same types compare by value; different types never compare equal.
  bool operator==(const Json& o) const;
  bool operator!=(const Json& o) const { return !(*this == o); }

 private:
  static constexpr std::size_t kNull = 0;
  static constexpr std::size_t kBool = 1;
  static constexpr std::size_t kNumber = 2;
  static constexpr std::size_t kString = 3;
  static constexpr std::size_t kArray = 4;
  static constexpr std::size_t kObject = 5;

  using Value =
      std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
  Value v_;

  static const Json& null_value();
  static const std::string& null_string();

  void dump_to(std::string& out) const;
};

// The plan writes support::Json throughout. This adds a minimal alias in the
// global namespace where the class lives, so the whole json.h doesn't need
// indenting for one namespace (smallest possible diff).
namespace support {
using Json = ::Json;
}  // namespace support

// ---- Implementation (header-only, all inline) -----------------------------

inline const Json& Json::null_value() {
  static const Json kNull;
  return kNull;
}

inline const std::string& Json::null_string() {
  static const std::string kEmpty;
  return kEmpty;
}

inline const Json::Object& Json::ItemRange::obj() const {
  static const Object kEmpty{};
  if (self_ == nullptr || !self_->is_object()) return kEmpty;
  return std::get<Object>(self_->v_);
}

// ---- Scalar reads ---------------------------------------------------------

inline bool Json::as_bool(bool fallback) const {
  if (auto* p = std::get_if<bool>(&v_)) return *p;
  return fallback;
}

inline double Json::as_number(double fallback) const {
  if (auto* p = std::get_if<double>(&v_)) return *p;
  return fallback;
}

inline std::string Json::as_string(std::string fallback) const {
  if (auto* p = std::get_if<std::string>(&v_)) return *p;
  return fallback;
}

inline long Json::as_int(long fallback) const {
  if (auto* p = std::get_if<double>(&v_)) return static_cast<long>(*p);
  return fallback;
}

namespace json_detail {

// Appends one \uXXXX escape. Surrogate pairs are merged by the caller.
inline void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

inline int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Writes a string as a JSON literal (quotes included).
inline void dump_string(std::string& out, const std::string& s) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

// Numbers: strip a meaningless trailing .0 (1.0 -> 1) to keep dump output clean.
inline void dump_number(std::string& out, double n) {
  if (!std::isfinite(n)) {
    // NaN/Inf serialize as null — output must be valid JSON.
    out += "null";
    return;
  }
  if (n == static_cast<double>(static_cast<long long>(n)) &&
      n >= -1e15 && n <= 1e15) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
    out += buf;
    return;
  }
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.17g", n);
  out += buf;
}

}  // namespace json_detail

// ---- Serialization ---------------------------------------------------------

inline void Json::dump_to(std::string& out) const {
  switch (v_.index()) {
    case kNull:
      out += "null";
      break;
    case kBool:
      out += std::get<bool>(v_) ? "true" : "false";
      break;
    case kNumber:
      json_detail::dump_number(out, std::get<double>(v_));
      break;
    case kString:
      json_detail::dump_string(out, std::get<std::string>(v_));
      break;
    case kArray: {
      out.push_back('[');
      const Array& a = std::get<Array>(v_);
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (i != 0) out.push_back(',');
        a[i].dump_to(out);
      }
      out.push_back(']');
      break;
    }
    case kObject: {
      out.push_back('{');
      bool first = true;
      for (const auto& kv : std::get<Object>(v_)) {
        if (!first) out.push_back(',');
        first = false;
        json_detail::dump_string(out, kv.first);
        out.push_back(':');
        kv.second.dump_to(out);
      }
      out.push_back('}');
      break;
    }
    default: {
      // Unreachable: the variant is exhaustive. Write null defensively instead of crashing.
      out += "null";
      break;
    }
  }
}

inline std::string Json::dump() const {
  std::string out;
  out.reserve(64);
  dump_to(out);
  return out;
}

// ---- Object / array access --------------------------------------------------

inline const Json& Json::at(std::string_view key) const {
  if (const Object* o = std::get_if<Object>(&v_)) {
    auto it = o->find(std::string(key));
    if (it != o->end()) return it->second;
  }
  return null_value();
}

inline bool Json::has(std::string_view key) const {
  if (const Object* o = std::get_if<Object>(&v_)) {
    auto it = o->find(std::string(key));
    return it != o->end() && !it->second.is_null();
  }
  return false;
}

inline bool Json::erase(std::string_view key) {
  if (Object* o = std::get_if<Object>(&v_)) return o->erase(std::string(key)) > 0;
  return false;
}

// find/end: std::map-consistent semantics. end() returns the tail of a static
// empty object (the unified sentinel); find on a real object returns the hit,
// and every miss returns the same sentinel — so "miss == end()" always holds
// (including across temporaries), and it->second is read only when it != end().
inline Json::const_iterator Json::find(std::string_view key) const {
  if (const Object* o = std::get_if<Object>(&v_)) {
    auto it = o->find(std::string(key));
    if (it != o->end()) return it;
    // Empty object / miss: cannot return o->end() — that is this object's own
    // sentinel, not comparable with another Json instance's end(). Fall back to
    // the unified static sentinel.
  }
  return end();
}

inline Json::const_iterator Json::end() const {
  // The static empty object is the unified sentinel: non-object values, empty
  // objects, and find misses all return the same place.
  static const Object kEmpty;
  return kEmpty.end();
}

inline Json& Json::operator[](std::string_view key) {
  if (!std::holds_alternative<Object>(v_)) v_ = Object{};
  return std::get<Object>(v_)[std::string(key)];
}

inline std::string Json::get_string(std::string_view key,
                                    std::string fallback) const {
  const Json& v = at(key);
  if (v.is_string()) return std::get<std::string>(v.v_);
  return fallback;
}

inline double Json::get_number(std::string_view key, double fallback) const {
  const Json& v = at(key);
  if (v.is_number()) return std::get<double>(v.v_);
  return fallback;
}

inline long Json::get_int(std::string_view key, long fallback) const {
  const Json& v = at(key);
  if (v.is_number()) return static_cast<long>(std::get<double>(v.v_));
  return fallback;
}

inline bool Json::get_bool(std::string_view key, bool fallback) const {
  const Json& v = at(key);
  if (v.is_bool()) return std::get<bool>(v.v_);
  return fallback;
}

inline std::size_t Json::size() const {
  if (const Object* o = std::get_if<Object>(&v_)) return o->size();
  if (const Array* a = std::get_if<Array>(&v_)) return a->size();
  if (const std::string* s = std::get_if<std::string>(&v_)) return s->size();
  return 0;
}

inline const Json& Json::operator[](std::size_t index) const {
  if (const Array* a = std::get_if<Array>(&v_)) {
    if (index < a->size()) return (*a)[index];
  }
  return null_value();
}

inline Json& Json::operator[](std::size_t index) {
  if (!std::holds_alternative<Array>(v_)) v_ = Array{};
  Array& a = std::get<Array>(v_);
  if (index >= a.size()) a.resize(index + 1);
  return a[index];
}

inline void Json::push_back(Json v) {
  if (!std::holds_alternative<Array>(v_)) v_ = Array{};
  std::get<Array>(v_).push_back(std::move(v));
}

inline bool Json::operator==(const Json& o) const {
  if (v_.index() != o.v_.index()) return false;
  if (is_null()) return true;
  return v_ == o.v_;
}

// ---- Parsing ----------------------------------------------------------------

namespace json_detail {

// Single-value parser. Every failure path writes err and returns false; nothing throws.
class Parser {
 public:
  Parser(std::string_view text, std::string& err) : s_(text), err_(err) {}

  bool parse(Json& out) {
    skip_ws();
    if (pos_ >= s_.size()) return fail("empty input");
    if (!parse_value(out)) return false;
    skip_ws();
    if (pos_ != s_.size()) return fail("trailing characters after value");
    return true;
  }

 private:
  std::string_view s_;
  std::string& err_;
  std::size_t pos_ = 0;
  int depth_ = 0;

  static constexpr int kMaxDepth = 200;

  bool fail(const std::string& msg) {
    if (err_.empty()) err_ = msg + " at offset " + std::to_string(pos_);
    return false;
  }

  bool eof() const { return pos_ >= s_.size(); }
  char peek() const { return eof() ? '\0' : s_[pos_]; }

  void skip_ws() {
    while (!eof()) {
      char c = s_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool lit(std::string_view word) {
    if (s_.compare(pos_, word.size(), word) != 0) return fail("expected literal");
    pos_ += word.size();
    return true;
  }

  bool parse_value(Json& out) {
    if (eof()) return fail("unexpected end of input");
    switch (peek()) {
      case '{': return parse_object(out);
      case '[': return parse_array(out);
      case '"': {
        std::string s;
        if (!parse_string(s)) return false;
        out = Json(std::move(s));
        return true;
      }
      case 't':
        if (!lit("true")) return false;
        out = Json(true);
        return true;
      case 'f':
        if (!lit("false")) return false;
        out = Json(false);
        return true;
      case 'n':
        if (!lit("null")) return false;
        out = Json(nullptr);
        return true;
      default:
        return parse_number(out);
    }
  }

  bool parse_object(Json& out) {
    if (++depth_ > kMaxDepth) return fail("nesting too deep");
    ++pos_;  // '{'
    Json::Object obj;
    skip_ws();
    if (peek() == '}') {
      ++pos_;
      --depth_;
      out = Json(std::move(obj));
      return true;
    }
    while (true) {
      skip_ws();
      if (peek() != '"') return fail("expected object key");
      std::string key;
      if (!parse_string(key)) return false;
      skip_ws();
      if (peek() != ':') return fail("expected ':'");
      ++pos_;
      skip_ws();
      Json val;
      if (!parse_value(val)) return false;
      obj[std::move(key)] = std::move(val);
      skip_ws();
      char c = peek();
      if (c == ',') {
        ++pos_;
        continue;
      }
      if (c == '}') {
        ++pos_;
        break;
      }
      return fail("expected ',' or '}'");
    }
    --depth_;
    out = Json(std::move(obj));
    return true;
  }

  bool parse_array(Json& out) {
    if (++depth_ > kMaxDepth) return fail("nesting too deep");
    ++pos_;  // '['
    Json::Array arr;
    skip_ws();
    if (peek() == ']') {
      ++pos_;
      --depth_;
      out = Json(std::move(arr));
      return true;
    }
    while (true) {
      skip_ws();
      Json val;
      if (!parse_value(val)) return false;
      arr.push_back(std::move(val));
      skip_ws();
      char c = peek();
      if (c == ',') {
        ++pos_;
        continue;
      }
      if (c == ']') {
        ++pos_;
        break;
      }
      return fail("expected ',' or ']'");
    }
    --depth_;
    out = Json(std::move(arr));
    return true;
  }

  bool parse_string(std::string& out) {
    ++pos_;  // opening quote
    out.clear();
    while (true) {
      if (eof()) return fail("unterminated string");
      unsigned char c = static_cast<unsigned char>(s_[pos_]);
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (c == '\\') {
        ++pos_;
        if (eof()) return fail("unterminated escape");
        char e = s_[pos_];
        switch (e) {
          case '"': out.push_back('"'); ++pos_; break;
          case '\\': out.push_back('\\'); ++pos_; break;
          case '/': out.push_back('/'); ++pos_; break;
          case 'b': out.push_back('\b'); ++pos_; break;
          case 'f': out.push_back('\f'); ++pos_; break;
          case 'n': out.push_back('\n'); ++pos_; break;
          case 'r': out.push_back('\r'); ++pos_; break;
          case 't': out.push_back('\t'); ++pos_; break;
          case 'u': {
            ++pos_;
            std::uint32_t cp = 0;
            if (!parse_hex4(cp)) return false;
            // surrogate pair: a high surrogate must be followed by a \uXXXX low one.
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (s_.compare(pos_, 2, "\\u") != 0)
                return fail("unpaired high surrogate");
              pos_ += 2;
              std::uint32_t lo = 0;
              if (!parse_hex4(lo)) return false;
              if (lo < 0xDC00 || lo > 0xDFFF)
                return fail("invalid low surrogate");
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              return fail("unpaired low surrogate");
            }
            append_utf8(out, cp);
            break;
          }
          default:
            return fail("invalid escape sequence");
        }
        continue;
      }
      if (c < 0x20) return fail("raw control character in string");
      // Multi-byte UTF-8 sequences pass through byte by byte; validity is not
      // verified (matching nlohmann's leniency).
      out.push_back(static_cast<char>(c));
      ++pos_;
    }
  }

  bool parse_hex4(std::uint32_t& cp) {
    if (pos_ + 4 > s_.size()) return fail("truncated \\u escape");
    cp = 0;
    for (int i = 0; i < 4; ++i) {
      int h = hex_val(s_[pos_ + i]);
      if (h < 0) return fail("invalid hex digit in \\u escape");
      cp = (cp << 4) | static_cast<std::uint32_t>(h);
    }
    pos_ += 4;
    return true;
  }

  bool parse_number(Json& out) {
    std::size_t start = pos_;
    if (peek() == '-') ++pos_;
    if (eof()) return fail("truncated number");
    if (peek() == '0') {
      ++pos_;
    } else if (peek() >= '1' && peek() <= '9') {
      while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
    } else {
      return fail("invalid number");
    }
    if (peek() == '.') {
      ++pos_;
      if (eof() || peek() < '0' || peek() > '9') return fail("invalid fraction");
      while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++pos_;
      if (peek() == '+' || peek() == '-') ++pos_;
      if (eof() || peek() < '0' || peek() > '9') return fail("invalid exponent");
      while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
    }
    const double v = strtod(std::string(s_.substr(start, pos_ - start)).c_str(), nullptr);
    // Overflow literals ("1e999") silently become inf: treat as a parse error;
    // invalid JSON must never leak out.
    if (!std::isfinite(v)) return fail("number out of range");
    out = Json(v);
    return true;
  }
};

}  // namespace json_detail

inline Json Json::parse(std::string_view text, std::string& err) {
  err.clear();
  Json out;
  json_detail::Parser p(text, err);
  if (!p.parse(out)) return Json(nullptr);
  return out;
}

// ---- get<T> specializations (the uniform access entry) ----

template <>
inline bool Json::get<bool>() const { return as_bool(); }
template <>
inline double Json::get<double>() const { return as_number(); }
template <>
inline float Json::get<float>() const { return static_cast<float>(as_number()); }
template <>
inline int Json::get<int>() const { return static_cast<int>(as_number()); }
template <>
inline long Json::get<long>() const { return as_int(); }
template <>
inline long long Json::get<long long>() const {
  return static_cast<long long>(as_number());
}
template <>
inline std::string Json::get<std::string>() const { return as_string(); }

template <class T>
inline T Json::get(std::string_view key) const {
  return at(key).get<T>();
}

