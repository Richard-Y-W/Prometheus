#include <prometheus/integrity/canonical_json.hpp>

#include "ecmascript_number.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace prometheus::integrity {
namespace {

using Json = nlohmann::json;
constexpr std::uint64_t safe_integer_max = 9007199254740991ULL;
constexpr std::string_view execution_component_schema =
    "urn:prometheus:schema:execution-component:2.0.0";
constexpr std::string_view execution_component_schema_version = "2.0.0";

[[noreturn]] void fail(std::string code, std::string message) {
  throw CanonicalJsonError(std::move(code), std::move(message));
}

bool is_valid_utf8(const std::string_view source) {
  std::size_t index = 0;
  while (index < source.size()) {
    const auto first = static_cast<unsigned char>(source[index]);
    if (first <= 0x7FU) {
      ++index;
      continue;
    }

    std::size_t width = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
      width = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      width = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      width = 4U;
    } else {
      return false;
    }
    if (index + width > source.size()) {
      return false;
    }
    for (std::size_t offset = 1U; offset < width; ++offset) {
      const auto continuation =
          static_cast<unsigned char>(source[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        return false;
      }
    }

    const auto second = static_cast<unsigned char>(source[index + 1U]);
    if (first == 0xE0U && second < 0xA0U) {
      return false;
    }
    if (first == 0xEDU && second > 0x9FU) {
      return false;
    }
    if (first == 0xF0U && second < 0x90U) {
      return false;
    }
    if (first == 0xF4U && second > 0x8FU) {
      return false;
    }
    index += width;
  }
  return true;
}

bool is_token_start_boundary(const std::string_view source,
                             const std::size_t index) {
  if (index == 0U) {
    return true;
  }
  switch (source[index - 1U]) {
  case ' ':
  case '\t':
  case '\n':
  case '\r':
  case '[':
  case '{':
  case ':':
  case ',':
    return true;
  default:
    return false;
  }
}

bool is_token_end(const char value) {
  switch (value) {
  case ' ':
  case '\t':
  case '\n':
  case '\r':
  case '[':
  case ']':
  case '{':
  case '}':
  case ':':
  case ',':
    return true;
  default:
    return false;
  }
}

struct NumberToken final {
  bool valid{false};
  bool floating_point{false};
  bool negative{false};
  bool mantissa_has_nonzero_digit{false};
  std::string_view integer_digits;
};

NumberToken inspect_number_token(const std::string_view token) {
  NumberToken result;
  std::size_t index = 0U;
  if (index < token.size() && token[index] == '-') {
    result.negative = true;
    ++index;
  }
  if (index == token.size()) {
    return result;
  }

  const auto integer_start = index;
  if (token[index] == '0') {
    ++index;
    if (index < token.size() && token[index] >= '0' && token[index] <= '9') {
      return result;
    }
  } else if (token[index] >= '1' && token[index] <= '9') {
    result.mantissa_has_nonzero_digit = true;
    while (index < token.size() && token[index] >= '0' &&
           token[index] <= '9') {
      ++index;
    }
  } else {
    return result;
  }
  const auto integer_end = index;

  if (index < token.size() && token[index] == '.') {
    result.floating_point = true;
    ++index;
    const auto fraction_start = index;
    while (index < token.size() && token[index] >= '0' &&
           token[index] <= '9') {
      result.mantissa_has_nonzero_digit =
          result.mantissa_has_nonzero_digit || token[index] != '0';
      ++index;
    }
    if (fraction_start == index) {
      return result;
    }
  }

  if (index < token.size() && (token[index] == 'e' || token[index] == 'E')) {
    result.floating_point = true;
    ++index;
    if (index < token.size() &&
        (token[index] == '+' || token[index] == '-')) {
      ++index;
    }
    const auto exponent_start = index;
    while (index < token.size() && token[index] >= '0' &&
           token[index] <= '9') {
      ++index;
    }
    if (exponent_start == index) {
      return result;
    }
  }

  if (index != token.size()) {
    return result;
  }
  result.valid = true;
  result.integer_digits =
      token.substr(integer_start, integer_end - integer_start);
  return result;
}

void enforce_integer_policy(const NumberToken &token) {
  auto significant = token.integer_digits;
  while (significant.size() > 1U && significant.front() == '0') {
    significant.remove_prefix(1U);
  }
  if (token.negative && significant == "0") {
    fail("negative_zero", "negative zero is not permitted");
  }
  constexpr std::string_view safe_limit = "9007199254740991";
  if (significant.size() > safe_limit.size() ||
      (significant.size() == safe_limit.size() && significant > safe_limit)) {
    fail("unsafe_integer",
         "integer is outside the interoperable binary64 safe-integer range");
  }
}

void scan_raw_numeric_policy(const std::string_view source) {
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = 0U; index < source.size();) {
    const auto value = source[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        in_string = false;
      }
      ++index;
      continue;
    }
    if (value == '"') {
      in_string = true;
      ++index;
      continue;
    }
    if (!is_token_start_boundary(source, index)) {
      ++index;
      continue;
    }
    if (!(value == '-' || (value >= '0' && value <= '9') || value == 'N' ||
          value == 'I')) {
      ++index;
      continue;
    }

    auto end = index + 1U;
    while (end < source.size() && !is_token_end(source[end])) {
      ++end;
    }
    const auto token = source.substr(index, end - index);
    if (token == "NaN" || token == "Infinity" || token == "-Infinity") {
      fail("non_finite_number", "non-finite number token is not permitted");
    }

    const auto number = inspect_number_token(token);
    if (number.valid) {
      if (!number.floating_point) {
        enforce_integer_policy(number);
      } else if (number.negative && !number.mantissa_has_nonzero_digit) {
        fail("negative_zero", "negative zero is not permitted");
      }
    }
    index = end;
  }
}

bool token_has_nonzero_mantissa_digit(const std::string_view token) {
  for (const auto value : token) {
    if (value == 'e' || value == 'E') {
      return false;
    }
    if (value >= '1' && value <= '9') {
      return true;
    }
  }
  return false;
}

class BoundedSax final : public nlohmann::json_sax<Json> {
public:
  explicit BoundedSax(const Limits limits) : limits_(limits) {}

  bool null() override {
    append_scalar(nullptr);
    return true;
  }

  bool boolean(const bool value) override {
    append_scalar(value);
    return true;
  }

  bool number_integer(const number_integer_t value) override {
    if (value < -static_cast<number_integer_t>(safe_integer_max) ||
        value > static_cast<number_integer_t>(safe_integer_max)) {
      fail("unsafe_integer",
           "integer is outside the interoperable binary64 safe-integer range");
    }
    append_scalar(value);
    return true;
  }

  bool number_unsigned(const number_unsigned_t value) override {
    if (value > safe_integer_max) {
      fail("unsafe_integer",
           "integer is outside the interoperable binary64 safe-integer range");
    }
    append_scalar(value);
    return true;
  }

  bool number_float(const number_float_t value,
                    const string_t &token) override {
    if (!std::isfinite(value)) {
      fail("number_overflow", "number overflows finite IEEE-754 binary64");
    }
    if (value == 0.0) {
      if (token_has_nonzero_mantissa_digit(token)) {
        fail("number_underflow", "nonzero number underflows to binary64 zero");
      }
      if (!token.empty() && token.front() == '-') {
        fail("negative_zero", "negative zero is not permitted");
      }
    }
    append_scalar(value);
    return true;
  }

  bool string(string_t &value) override {
    check_string(value);
    append_scalar(std::move(value));
    return true;
  }

  bool binary(binary_t &) override {
    fail("invalid_json", "binary values are not JSON values");
  }

  bool start_object(const std::size_t) override {
    begin_container(FrameKind::object);
    return true;
  }

  bool key(string_t &value) override {
    if (frames_.empty() || frames_.back().kind != FrameKind::object ||
        frames_.back().pending_key.has_value()) {
      fail("invalid_json", "invalid object key position");
    }
    check_string(value);
    auto &frame = frames_.back();
    if (!frame.decoded_keys.insert(value).second) {
      fail("duplicate_key", "duplicate decoded object key");
    }
    ++frame.child_count;
    if (frame.child_count > limits_.object_members) {
      fail("max_object_members_exceeded",
           "object exceeds configured member limit");
    }
    frame.pending_key = std::move(value);
    return true;
  }

  bool end_object() override {
    end_container(FrameKind::object);
    return true;
  }

  bool start_array(const std::size_t) override {
    begin_container(FrameKind::array);
    return true;
  }

  bool end_array() override {
    end_container(FrameKind::array);
    return true;
  }

  bool parse_error(const std::size_t, const std::string &,
                   const nlohmann::detail::exception &error) override {
    const std::string message = error.what();
    if (error.id == 406 || message.find("number overflow") != std::string::npos) {
      fail("number_overflow", "number overflows finite IEEE-754 binary64");
    }
    if (message.find("surrogate") != std::string::npos) {
      fail("invalid_unicode", "string contains an invalid Unicode surrogate");
    }
    fail("invalid_json", "input is not valid JSON");
  }

  [[nodiscard]] Json take_root() {
    if (!root_.has_value() || !frames_.empty()) {
      fail("invalid_json", "input does not contain one complete JSON value");
    }
    return std::move(*root_);
  }

private:
  enum class FrameKind { object, array };

  struct Frame final {
    explicit Frame(const FrameKind frame_kind)
        : kind(frame_kind), value(frame_kind == FrameKind::object
                                      ? Json::object()
                                      : Json::array()) {}

    FrameKind kind;
    Json value;
    std::unordered_set<std::string> decoded_keys;
    std::optional<std::string> pending_key;
    std::size_t child_count{0U};
  };

  void count_node() {
    ++node_count_;
    if (node_count_ > limits_.nodes) {
      fail("max_nodes_exceeded", "JSON value exceeds configured node limit");
    }
  }

  void check_string(const std::string_view value) const {
    if (value.size() > limits_.string_bytes) {
      fail("max_string_bytes_exceeded",
           "UTF-8 string exceeds configured byte limit");
    }
  }

  template <typename Value> void append_scalar(Value &&value) {
    count_node();
    append_value(Json(std::forward<Value>(value)));
  }

  void begin_container(const FrameKind kind) {
    count_node();
    if (frames_.size() + 1U > limits_.depth) {
      fail("max_depth_exceeded", "JSON nesting exceeds configured depth limit");
    }
    frames_.emplace_back(kind);
  }

  void end_container(const FrameKind kind) {
    if (frames_.empty() || frames_.back().kind != kind ||
        (kind == FrameKind::object &&
         frames_.back().pending_key.has_value())) {
      fail("invalid_json", "mismatched JSON container boundary");
    }
    auto completed = std::move(frames_.back().value);
    frames_.pop_back();
    append_value(std::move(completed));
  }

  void append_value(Json value) {
    if (frames_.empty()) {
      if (root_.has_value()) {
        fail("invalid_json", "input contains more than one JSON value");
      }
      root_ = std::move(value);
      return;
    }

    auto &frame = frames_.back();
    if (frame.kind == FrameKind::array) {
      ++frame.child_count;
      if (frame.child_count > limits_.array_elements) {
        fail("max_array_elements_exceeded",
             "array exceeds configured element limit");
      }
      frame.value.push_back(std::move(value));
      return;
    }

    if (!frame.pending_key.has_value()) {
      fail("invalid_json", "object value is missing its key");
    }
    auto key_value = std::move(*frame.pending_key);
    frame.pending_key.reset();
    frame.value.emplace(std::move(key_value), std::move(value));
  }

  Limits limits_;
  std::vector<Frame> frames_;
  std::optional<Json> root_;
  std::size_t node_count_{0U};
};

Json parse_json(const std::string_view source, const Limits limits) {
  if (source.size() > limits.raw_bytes) {
    fail("max_raw_bytes_exceeded", "raw JSON exceeds configured byte limit");
  }
  if (!is_valid_utf8(source)) {
    fail("invalid_utf8", "input is not valid UTF-8");
  }
  if (source.size() >= 3U &&
      static_cast<unsigned char>(source[0]) == 0xEFU &&
      static_cast<unsigned char>(source[1]) == 0xBBU &&
      static_cast<unsigned char>(source[2]) == 0xBFU) {
    fail("utf8_bom", "UTF-8 byte-order marks are not permitted");
  }
  scan_raw_numeric_policy(source);

  BoundedSax handler(limits);
  const auto parsed = Json::sax_parse(source.begin(), source.end(), &handler,
                                      Json::input_format_t::json, true, false);
  if (!parsed) {
    fail("invalid_json", "input is not valid JSON");
  }
  return handler.take_root();
}

std::uint32_t decode_code_point(const std::string_view value,
                                std::size_t &index) {
  const auto first = static_cast<unsigned char>(value[index]);
  if (first <= 0x7FU) {
    ++index;
    return first;
  }
  if (first <= 0xDFU) {
    const auto result =
        static_cast<std::uint32_t>(first & 0x1FU) << 6U |
        (static_cast<unsigned char>(value[index + 1U]) & 0x3FU);
    index += 2U;
    return result;
  }
  if (first <= 0xEFU) {
    const auto result =
        static_cast<std::uint32_t>(first & 0x0FU) << 12U |
        static_cast<std::uint32_t>(
            static_cast<unsigned char>(value[index + 1U]) & 0x3FU)
            << 6U |
        (static_cast<unsigned char>(value[index + 2U]) & 0x3FU);
    index += 3U;
    return result;
  }
  const auto result =
      static_cast<std::uint32_t>(first & 0x07U) << 18U |
      static_cast<std::uint32_t>(
          static_cast<unsigned char>(value[index + 1U]) & 0x3FU)
          << 12U |
      static_cast<std::uint32_t>(
          static_cast<unsigned char>(value[index + 2U]) & 0x3FU)
          << 6U |
      (static_cast<unsigned char>(value[index + 3U]) & 0x3FU);
  index += 4U;
  return result;
}

std::vector<std::uint16_t> utf16_sort_key(const std::string_view value) {
  std::vector<std::uint16_t> result;
  result.reserve(value.size());
  for (std::size_t index = 0U; index < value.size();) {
    const auto code_point = decode_code_point(value, index);
    if (code_point <= 0xFFFFU) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      const auto supplementary = code_point - 0x10000U;
      result.push_back(
          static_cast<std::uint16_t>(0xD800U + (supplementary >> 10U)));
      result.push_back(static_cast<std::uint16_t>(
          0xDC00U + (supplementary & 0x3FFU)));
    }
  }
  return result;
}

void append_string(const std::string_view value, std::string &output) {
  constexpr char hex[] = "0123456789abcdef";
  output.push_back('"');
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    switch (byte) {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\b':
      output += "\\b";
      break;
    case '\f':
      output += "\\f";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (byte <= 0x1FU) {
        output += "\\u00";
        output.push_back(hex[(byte >> 4U) & 0x0FU]);
        output.push_back(hex[byte & 0x0FU]);
      } else {
        output.push_back(character);
      }
      break;
    }
  }
  output.push_back('"');
}

void serialize_value(const Json &value, std::string &output) {
  if (value.is_null()) {
    output += "null";
    return;
  }
  if (value.is_boolean()) {
    output += value.get<bool>() ? "true" : "false";
    return;
  }
  if (value.is_number_integer()) {
    output += std::to_string(value.get<Json::number_integer_t>());
    return;
  }
  if (value.is_number_unsigned()) {
    output += std::to_string(value.get<Json::number_unsigned_t>());
    return;
  }
  if (value.is_number_float()) {
    output += detail::format_ecmascript_number(value.get<double>());
    return;
  }
  if (value.is_string()) {
    append_string(value.get_ref<const std::string &>(), output);
    return;
  }
  if (value.is_array()) {
    output.push_back('[');
    bool first = true;
    for (const auto &element : value) {
      if (!first) {
        output.push_back(',');
      }
      serialize_value(element, output);
      first = false;
    }
    output.push_back(']');
    return;
  }
  if (value.is_object()) {
    struct Member final {
      std::string key;
      const Json *value;
      std::vector<std::uint16_t> sort_key;
    };
    std::vector<Member> members;
    members.reserve(value.size());
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      members.push_back(
          {iterator.key(), &iterator.value(), utf16_sort_key(iterator.key())});
    }
    std::sort(members.begin(), members.end(),
              [](const Member &left, const Member &right) {
                return left.sort_key < right.sort_key;
              });

    output.push_back('{');
    bool first = true;
    for (const auto &member : members) {
      if (!first) {
        output.push_back(',');
      }
      append_string(member.key, output);
      output.push_back(':');
      serialize_value(*member.value, output);
      first = false;
    }
    output.push_back('}');
    return;
  }
  fail("canonicalization_failed", "unsupported JSON value type");
}

std::string serialize_json(const Json &value, const Limits limits) {
  std::string output;
  serialize_value(value, output);
  if (output.size() > limits.raw_bytes) {
    fail("max_canonical_bytes_exceeded",
         "canonical JSON exceeds configured byte limit");
  }
  return output;
}

std::string sha256_identity(const std::string_view source) {
  QCryptographicHash hasher(QCryptographicHash::Sha256);
  hasher.addData(QByteArrayView(source.data(),
                               static_cast<qsizetype>(source.size())));
  return "sha256:" + hasher.result().toHex().toStdString();
}

} // namespace

CanonicalJsonError::CanonicalJsonError(std::string code, std::string message)
    : std::runtime_error(std::move(message)), code_(std::move(code)) {}

const std::string &CanonicalJsonError::code() const noexcept { return code_; }

std::string canonicalize_json_bytes(const std::string_view source,
                                    const Limits limits) {
  const auto value = parse_json(source, limits);
  const auto canonical = serialize_json(value, limits);
  static_cast<void>(parse_json(canonical, limits));
  return canonical;
}

std::string verify_canonical_bytes(const std::string_view source,
                                   const Limits limits) {
  const auto canonical = canonicalize_json_bytes(source, limits);
  if (canonical != source) {
    fail("noncanonical_bytes", "JSON bytes are valid but not canonical");
  }
  return std::string(source);
}

std::string object_hash(const std::string_view canonical_bytes) {
  const auto verified = verify_canonical_bytes(canonical_bytes);
  return sha256_identity(verified);
}

std::string verify_execution_component(
    const std::string_view stored_bytes,
    const std::string_view expected_object_hash, const Limits limits) {
  const auto verified = verify_canonical_bytes(stored_bytes, limits);
  const auto actual_hash = sha256_identity(verified);
  if (actual_hash != expected_object_hash) {
    fail("object_hash_mismatch",
         "stored bytes do not match the expected object hash");
  }

  const auto value = parse_json(verified, limits);
  if (!value.is_object() || !value.contains("$schema") ||
      !value.at("$schema").is_string() ||
      value.at("$schema").get_ref<const std::string &>() !=
          execution_component_schema ||
      !value.contains("schema_version") ||
      !value.at("schema_version").is_string() ||
      value.at("schema_version").get_ref<const std::string &>() !=
          execution_component_schema_version) {
    fail("unsupported_schema",
         "execution component schema identity is not supported");
  }
  return actual_hash;
}

} // namespace prometheus::integrity
