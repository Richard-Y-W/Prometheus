#include <prometheus/integrity/canonical_json.hpp>

#include "ecmascript_number.hpp"

#include <nlohmann/json.hpp>
#include <picosha2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

std::string raw_sha256_identity(const std::string_view bytes) {
  return "sha256:" +
         picosha2::hash256_hex_string(bytes.begin(), bytes.end());
}

bool contains_key(const std::initializer_list<std::string_view> keys,
                  const std::string_view candidate) {
  return std::find(keys.begin(), keys.end(), candidate) != keys.end();
}

void require_exact_members(
    const Json &value, const std::initializer_list<std::string_view> keys) {
  if (!value.is_object()) {
    fail("invalid_type", "execution-component member must be an object");
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!contains_key(keys, iterator.key())) {
      fail("unknown_field", "execution-component object has an unknown field");
    }
  }
  for (const auto key : keys) {
    if (!value.contains(std::string(key))) {
      fail("missing_field", "execution-component object is missing a field");
    }
  }
}

const std::string &required_string(const Json &object,
                                   const std::string_view key) {
  const auto &value = object.at(std::string(key));
  if (!value.is_string() || value.get_ref<const std::string &>().empty()) {
    fail("invalid_identity", "execution-component string identity is invalid");
  }
  return value.get_ref<const std::string &>();
}

const Json &required_array(const Json &object, const std::string_view key) {
  const auto &value = object.at(std::string(key));
  if (!value.is_array()) {
    fail("invalid_type", "execution-component member must be an array");
  }
  return value;
}

std::uint64_t required_nonnegative_integer(const Json &object,
                                           const std::string_view key) {
  const auto &value = object.at(std::string(key));
  if (!value.is_number_integer() && !value.is_number_unsigned()) {
    fail("invalid_type", "execution-component member must be an integer");
  }
  if (value.is_number_integer() && value.get<std::int64_t>() < 0) {
    fail("invalid_type", "execution-component integer must be nonnegative");
  }
  return value.get<std::uint64_t>();
}

bool is_hash_id(const std::string_view value) {
  if (value.size() != 71U || !value.starts_with("sha256:")) {
    return false;
  }
  return std::all_of(value.begin() + 7, value.end(), [](const char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

void require_hash_id(const std::string_view value) {
  if (!is_hash_id(value)) {
    fail("invalid_hash", "execution-component hash identity is invalid");
  }
}

void validate_engineering_value(const Json &value,
                                const std::string_view value_state) {
  if (!value.is_object()) {
    fail("invalid_value_shape", "claim value must be an object");
  }
  if (!value.contains("kind")) {
    fail("missing_field", "claim value is missing its kind discriminator");
  }
  const auto &kind = required_string(value, "kind");
  if (value_state == "unknown") {
    require_exact_members(value, {"kind", "reason"});
    if (kind != "unknown") {
      fail("invalid_value_shape", "unknown claim must use unknown value kind");
    }
    static_cast<void>(required_string(value, "reason"));
    return;
  }

  if (kind == "scalar") {
    require_exact_members(value, {"kind", "value"});
    if (!value.at("value").is_number()) {
      fail("invalid_value_shape", "scalar claim must contain a number");
    }
    return;
  }
  if (kind == "range") {
    require_exact_members(value, {"kind", "minimum", "maximum"});
    if (!value.at("minimum").is_number() ||
        !value.at("maximum").is_number() ||
        value.at("minimum").get<double>() >
            value.at("maximum").get<double>()) {
      fail("invalid_value_shape", "range claim has invalid bounds");
    }
    return;
  }
  if (kind == "enumeration") {
    require_exact_members(value, {"kind", "values"});
    const auto &values = required_array(value, "values");
    if (values.empty()) {
      fail("invalid_value_shape", "enumeration claim must not be empty");
    }
    std::unordered_set<std::string> identities;
    for (const auto &item : values) {
      if (!item.is_string() && !item.is_number() && !item.is_boolean()) {
        fail("invalid_value_shape", "enumeration claim item is invalid");
      }
      const auto identity = serialize_json(item, Limits{});
      if (!identities.insert(identity).second) {
        fail("invalid_value_shape", "enumeration claim items must be unique");
      }
    }
    return;
  }
  if (kind == "curve") {
    require_exact_members(value,
                          {"kind", "independent_quantity", "independent_unit",
                           "interpolation", "points"});
    static_cast<void>(required_string(value, "independent_quantity"));
    static_cast<void>(required_string(value, "independent_unit"));
    const auto &interpolation = required_string(value, "interpolation");
    if (interpolation != "linear" && interpolation != "step" &&
        interpolation != "cubic") {
      fail("invalid_value_shape", "curve interpolation is invalid");
    }
    const auto &points = required_array(value, "points");
    if (points.size() < 2U) {
      fail("invalid_value_shape", "curve requires at least two points");
    }
    std::optional<double> previous_x;
    for (const auto &point : points) {
      require_exact_members(point, {"x", "y"});
      if (!point.at("x").is_number() || !point.at("y").is_number()) {
        fail("invalid_value_shape", "curve point must contain numbers");
      }
      const auto x = point.at("x").get<double>();
      if (previous_x.has_value() && x <= *previous_x) {
        fail("invalid_value_shape", "curve x values must increase");
      }
      previous_x = x;
    }
    return;
  }
  fail("invalid_value_shape", "claim value kind is unsupported");
}

std::string recompute_claim_fingerprint(const Json &claim,
                                        const Limits limits) {
  auto evidence_ids =
      claim.at("evidence_ids").get<std::vector<std::string>>();
  std::sort(evidence_ids.begin(), evidence_ids.end());
  Json semantic = {
      {"revision_id", claim.at("revision_id")},
      {"slot_id", claim.at("slot_id")},
      {"value_state", claim.at("value_state")},
      {"value", claim.at("value")},
      {"provenance", claim.at("provenance")},
      {"evidence_ids", std::move(evidence_ids)},
      {"validity_conditions", claim.at("validity_conditions")},
  };
  if (claim.at("value_state") == "known") {
    semantic["unit"] = claim.at("unit");
    semantic["original_value"] = claim.at("original_value");
    semantic["original_unit"] = claim.at("original_unit");
  }
  return raw_sha256_identity(serialize_json(semantic, limits));
}

void validate_execution_component_graph(const Json &root,
                                        const Limits limits) {
  require_exact_members(
      root,
      {"$schema", "schema_version", "package_kind", "capability_id",
       "revision_id", "reviewed_draft_version", "component", "authority",
       "package_compiler", "artifacts", "parameter_slots", "claims",
       "evidence", "claim_reviews", "gates", "missing_information",
       "limitations", "execution_readiness"});

  if (required_string(root, "package_kind") != "component_execution_input") {
    fail("unsupported_package_kind", "execution-component kind is unsupported");
  }
  const auto &capability_id = required_string(root, "capability_id");
  const auto &revision_id = required_string(root, "revision_id");
  const auto reviewed_draft_version =
      required_nonnegative_integer(root, "reviewed_draft_version");
  if (reviewed_draft_version == 0U) {
    fail("invalid_review_version", "reviewed draft version must be positive");
  }
  const auto &readiness = required_string(root, "execution_readiness");
  if (readiness != "ready" && readiness != "blocked") {
    fail("invalid_execution_readiness",
         "execution readiness must be ready or blocked");
  }

  const auto &component = root.at("component");
  require_exact_members(component,
                        {"component_id", "manufacturer", "part_number",
                         "revision", "component_class"});
  const auto &component_id = required_string(component, "component_id");
  static_cast<void>(required_string(component, "manufacturer"));
  static_cast<void>(required_string(component, "part_number"));
  static_cast<void>(required_string(component, "revision"));
  static_cast<void>(required_string(component, "component_class"));

  const auto &authority = root.at("authority");
  require_exact_members(authority,
                        {"authority_role", "engineering_decision_authority",
                         "package_role"});
  if (required_string(authority, "authority_role") != "input_only" ||
      required_string(authority, "engineering_decision_authority") !=
          "prometheus_cpp" ||
      required_string(authority, "package_role") != "reviewed_input") {
    fail("invalid_authority", "execution-component authority is invalid");
  }

  const auto &compiler = root.at("package_compiler");
  require_exact_members(compiler, {"name", "version"});
  static_cast<void>(required_string(compiler, "name"));
  static_cast<void>(required_string(compiler, "version"));

  std::unordered_set<std::string> artifact_hashes;
  std::vector<std::string> artifact_order;
  for (const auto &artifact : required_array(root, "artifacts")) {
    require_exact_members(artifact,
                          {"artifact_hash", "filename", "media_type",
                           "byte_length", "artifact_role"});
    const auto &artifact_hash = required_string(artifact, "artifact_hash");
    require_hash_id(artifact_hash);
    if (!artifact_hashes.insert(artifact_hash).second) {
      fail("duplicate_artifact", "artifact hashes must be unique");
    }
    artifact_order.push_back(artifact_hash);
    static_cast<void>(required_string(artifact, "filename"));
    static_cast<void>(required_string(artifact, "media_type"));
    static_cast<void>(required_nonnegative_integer(artifact, "byte_length"));
    const auto &role = required_string(artifact, "artifact_role");
    if (role != "source_evidence" && role != "supporting_input") {
      fail("invalid_artifact_role", "artifact role is unsupported");
    }
  }
  if (artifact_order.empty()) {
    fail("missing_artifact", "execution-component requires an artifact");
  }
  if (!std::is_sorted(artifact_order.begin(), artifact_order.end())) {
    fail("invalid_contract_order", "artifacts are not contract ordered");
  }

  std::unordered_set<std::string> slot_ids;
  std::unordered_set<std::string> slot_names;
  std::unordered_set<std::string> selected_claim_ids;
  std::unordered_map<std::string, const Json *> slots_by_claim;
  std::vector<std::pair<std::string, std::string>> slot_order;
  for (const auto &slot : required_array(root, "parameter_slots")) {
    require_exact_members(slot,
                          {"slot_id", "name", "quantity", "dimension",
                           "required_for_execution", "selected_claim_id"});
    const auto &slot_id = required_string(slot, "slot_id");
    const auto &name = required_string(slot, "name");
    const auto &selected_claim_id =
        required_string(slot, "selected_claim_id");
    static_cast<void>(required_string(slot, "quantity"));
    static_cast<void>(required_string(slot, "dimension"));
    if (!slot.at("required_for_execution").is_boolean()) {
      fail("invalid_type", "slot required flag must be boolean");
    }
    if (!slot_ids.insert(slot_id).second) {
      fail("duplicate_slot_id", "parameter slot IDs must be unique");
    }
    if (!slot_names.insert(name).second) {
      fail("duplicate_slot_name", "parameter slot names must be unique");
    }
    if (!selected_claim_ids.insert(selected_claim_id).second) {
      fail("duplicate_selected_claim", "one claim cannot satisfy two slots");
    }
    slots_by_claim.emplace(selected_claim_id, &slot);
    slot_order.emplace_back(name, slot_id);
  }
  if (!std::is_sorted(slot_order.begin(), slot_order.end())) {
    fail("invalid_contract_order", "parameter slots are not contract ordered");
  }

  std::unordered_set<std::string> evidence_ids;
  std::vector<std::string> evidence_order;
  for (const auto &record : required_array(root, "evidence")) {
    if (!record.is_object() || !record.contains("evidence_id") ||
        !record.contains("revision_id") || !record.contains("evidence_class")) {
      fail("missing_field", "evidence identity is incomplete");
    }
    const auto &evidence_id = required_string(record, "evidence_id");
    if (!evidence_ids.insert(evidence_id).second) {
      fail("duplicate_evidence", "evidence IDs must be unique");
    }
    evidence_order.push_back(evidence_id);
    if (required_string(record, "revision_id") != revision_id) {
      fail("cross_revision_evidence", "evidence belongs to another revision");
    }
    static_cast<void>(required_string(record, "evidence_class"));
    if (record.contains("artifact_hash") &&
        !record.at("artifact_hash").is_null()) {
      const auto &artifact_hash = required_string(record, "artifact_hash");
      if (!artifact_hashes.contains(artifact_hash)) {
        fail("missing_artifact_reference",
             "evidence references an absent artifact");
      }
    }
  }
  if (!std::is_sorted(evidence_order.begin(), evidence_order.end())) {
    fail("invalid_contract_order", "evidence is not contract ordered");
  }

  std::unordered_map<std::string, const Json *> claims;
  std::vector<std::string> claim_order;
  for (const auto &claim : required_array(root, "claims")) {
    if (!claim.is_object() || !claim.contains("value_state")) {
      fail("missing_field", "claim identity is incomplete");
    }
    const auto &value_state = required_string(claim, "value_state");
    if (value_state == "known") {
      require_exact_members(
          claim,
          {"claim_id", "revision_id", "slot_id", "value_state", "value",
           "validity_conditions", "provenance", "evidence_ids",
           "claim_fingerprint", "unit", "original_value", "original_unit"});
    } else if (value_state == "unknown") {
      require_exact_members(
          claim,
          {"claim_id", "revision_id", "slot_id", "value_state", "value",
           "validity_conditions", "provenance", "evidence_ids",
           "claim_fingerprint"});
    } else {
      fail("invalid_value_state", "claim value state is unsupported");
    }
    const auto &claim_id = required_string(claim, "claim_id");
    if (!claims.emplace(claim_id, &claim).second) {
      fail("duplicate_claim", "claim IDs must be unique");
    }
    claim_order.push_back(claim_id);
    if (required_string(claim, "revision_id") != revision_id) {
      fail("cross_revision_claim", "claim belongs to another revision");
    }
    const auto &slot_id = required_string(claim, "slot_id");
    if (!slot_ids.contains(slot_id)) {
      fail("selected_claim_link_mismatch", "claim references an absent slot");
    }
    static_cast<void>(required_string(claim, "provenance"));
    const auto &claim_evidence = required_array(claim, "evidence_ids");
    std::unordered_set<std::string> unique_claim_evidence;
    std::vector<std::string> ordered_claim_evidence;
    for (const auto &evidence_id_value : claim_evidence) {
      if (!evidence_id_value.is_string() ||
          !evidence_ids.contains(evidence_id_value.get<std::string>())) {
        fail("missing_evidence_reference",
             "claim references evidence absent from the package");
      }
      const auto evidence_id = evidence_id_value.get<std::string>();
      if (!unique_claim_evidence.insert(evidence_id).second) {
        fail("duplicate_claim_evidence",
             "claim evidence references must be unique");
      }
      ordered_claim_evidence.push_back(evidence_id);
    }
    if (!std::is_sorted(ordered_claim_evidence.begin(),
                        ordered_claim_evidence.end())) {
      fail("invalid_contract_order",
           "claim evidence references are not contract ordered");
    }
    const auto &validity_conditions =
        required_array(claim, "validity_conditions");
    std::unordered_set<std::string> unique_validity_conditions;
    for (const auto &condition : validity_conditions) {
      if (!condition.is_string() || condition.get_ref<const std::string &>().empty()) {
        fail("invalid_type", "claim validity condition is invalid");
      }
      if (!unique_validity_conditions
               .insert(condition.get_ref<const std::string &>())
               .second) {
        fail("duplicate_validity_condition",
             "claim validity conditions must be unique");
      }
    }
    if (value_state == "known") {
      static_cast<void>(required_string(claim, "unit"));
      static_cast<void>(required_string(claim, "original_value"));
      static_cast<void>(required_string(claim, "original_unit"));
    }
    validate_engineering_value(claim.at("value"), value_state);
    const auto &fingerprint = required_string(claim, "claim_fingerprint");
    require_hash_id(fingerprint);
    if (fingerprint != recompute_claim_fingerprint(claim, limits)) {
      fail("claim_fingerprint_mismatch",
           "claim fingerprint does not match semantic claim fields");
    }
  }
  if (!std::is_sorted(claim_order.begin(), claim_order.end())) {
    fail("invalid_contract_order", "claims are not contract ordered");
  }
  if (claims.size() != slots_by_claim.size()) {
    fail("selected_claim_set_mismatch",
         "package must contain one selected claim per slot");
  }
  for (const auto &[claim_id, slot] : slots_by_claim) {
    const auto claim = claims.find(claim_id);
    if (claim == claims.end()) {
      fail("selected_claim_missing", "slot selects an absent claim");
    }
    if (required_string(*claim->second, "slot_id") !=
        required_string(*slot, "slot_id")) {
      fail("selected_claim_link_mismatch",
           "selected claim belongs to a different slot");
    }
  }

  std::unordered_set<std::string> review_event_ids;
  std::unordered_set<std::string> reviewed_claim_ids;
  std::vector<std::string> review_order;
  for (const auto &review : required_array(root, "claim_reviews")) {
    require_exact_members(
        review,
        {"review_event_id", "revision_id", "claim_id", "decision",
         "reviewed_by", "reviewed_at", "note", "applied_draft_version",
         "reviewed_claim_fingerprint"});
    const auto &review_event_id = required_string(review, "review_event_id");
    if (!review_event_ids.insert(review_event_id).second) {
      fail("duplicate_review", "review event IDs must be unique");
    }
    review_order.push_back(review_event_id);
    const auto &claim_id = required_string(review, "claim_id");
    if (!reviewed_claim_ids.insert(claim_id).second) {
      fail("duplicate_review", "a selected claim has multiple reviews");
    }
    const auto claim = claims.find(claim_id);
    if (claim == claims.end()) {
      fail("missing_review_claim", "review references an absent claim");
    }
    if (required_string(review, "revision_id") != revision_id) {
      fail("cross_revision_review", "review belongs to another revision");
    }
    if (required_string(review, "decision") != "accepted") {
      fail("review_not_accepted", "selected claim review is not accepted");
    }
    static_cast<void>(required_string(review, "reviewed_by"));
    static_cast<void>(required_string(review, "reviewed_at"));
    if (!review.at("note").is_string() ||
        review.at("note").get_ref<const std::string &>().empty()) {
      fail("invalid_review_note", "review note must be a nonempty string");
    }
    const auto applied_draft_version =
        required_nonnegative_integer(review, "applied_draft_version");
    if (applied_draft_version == 0U) {
      fail("invalid_review_version", "applied review version must be positive");
    }
    if (applied_draft_version > reviewed_draft_version) {
      fail("stale_review", "review applies to a later draft version");
    }
    const auto &reviewed_fingerprint =
        required_string(review, "reviewed_claim_fingerprint");
    if (reviewed_fingerprint !=
        required_string(*claim->second, "claim_fingerprint")) {
      fail("stale_review", "review fingerprint does not match selected claim");
    }
  }
  if (!std::is_sorted(review_order.begin(), review_order.end())) {
    fail("invalid_contract_order", "claim reviews are not contract ordered");
  }
  if (reviewed_claim_ids.size() != claims.size()) {
    fail("missing_claim_review", "every selected claim requires one review");
  }

  std::unordered_set<std::string> known_references{component_id};
  known_references.insert(artifact_hashes.begin(), artifact_hashes.end());
  for (const auto &[claim_id, ignored] : claims) {
    static_cast<void>(ignored);
    known_references.insert(claim_id);
  }
  known_references.insert(review_event_ids.begin(), review_event_ids.end());

  std::unordered_set<std::string> gate_ids;
  std::vector<std::string> gate_order;
  std::size_t publication_gate_count = 0U;
  bool execution_gates_satisfied = true;
  for (const auto &gate : required_array(root, "gates")) {
    require_exact_members(
        gate,
        {"gate_id", "capability_id", "phase", "state",
         "required_review_type", "satisfying_reference_ids", "reason"});
    const auto &gate_id = required_string(gate, "gate_id");
    if (!gate_ids.insert(gate_id).second) {
      fail("duplicate_gate", "gate IDs must be unique");
    }
    gate_order.push_back(gate_id);
    if (required_string(gate, "capability_id") != capability_id) {
      fail("gate_capability_mismatch", "gate capability is inconsistent");
    }
    const auto &phase = required_string(gate, "phase");
    if (phase != "publication" && phase != "execution") {
      fail("invalid_gate_phase", "gate phase is unsupported");
    }
    const auto &state = required_string(gate, "state");
    if (state != "satisfied" && state != "blocked" && state != "pending") {
      fail("invalid_gate_state", "gate state is unsupported");
    }
    if (state == "pending") {
      fail("unresolved_gate", "pending gate is unresolved");
    }
    static_cast<void>(required_string(gate, "required_review_type"));
    if (!gate.at("reason").is_null() && !gate.at("reason").is_string()) {
      fail("invalid_type", "gate reason must be a string or null");
    }
    std::unordered_set<std::string> gate_references;
    std::vector<std::string> ordered_gate_references;
    for (const auto &reference :
         required_array(gate, "satisfying_reference_ids")) {
      if (!reference.is_string()) {
        fail("invalid_type", "gate reference must be a string");
      }
      const auto reference_id = reference.get<std::string>();
      if (!gate_references.insert(reference_id).second) {
        fail("duplicate_gate_reference", "gate references must be unique");
      }
      ordered_gate_references.push_back(reference_id);
      if (!known_references.contains(reference_id)) {
        fail("missing_gate_reference",
             "gate references an absent package identity");
      }
    }
    if (!std::is_sorted(ordered_gate_references.begin(),
                        ordered_gate_references.end())) {
      fail("invalid_contract_order", "gate references are not contract ordered");
    }
    if (state == "satisfied") {
      if (ordered_gate_references.empty() || !gate.at("reason").is_null()) {
        fail("gate_state_metadata_mismatch",
             "satisfied gate requires references and no reason");
      }
    } else if (!ordered_gate_references.empty() ||
               !gate.at("reason").is_string() ||
               gate.at("reason").get_ref<const std::string &>().empty()) {
      fail("gate_state_metadata_mismatch",
           "blocked gate requires only an explicit reason");
    }
    if (phase == "publication") {
      ++publication_gate_count;
      if (state != "satisfied") {
        fail("unresolved_publication_gate",
             "publication gate must be satisfied");
      }
    } else if (state != "satisfied") {
      execution_gates_satisfied = false;
    }
  }
  if (!std::is_sorted(gate_order.begin(), gate_order.end())) {
    fail("invalid_contract_order", "gates are not contract ordered");
  }
  if (publication_gate_count == 0U) {
    fail("missing_publication_gate", "package has no publication gate");
  }
  const auto expected_readiness =
      execution_gates_satisfied ? std::string_view("ready")
                                : std::string_view("blocked");
  if (readiness != expected_readiness) {
    fail("readiness_gate_mismatch",
         "execution readiness disagrees with execution gates");
  }

  std::unordered_set<std::string> missing_information_ids;
  std::vector<std::string> missing_information_order;
  for (const auto &item : required_array(root, "missing_information")) {
    if (!item.is_object() || !item.contains("missing_information_id") ||
        !item.contains("slot_id")) {
      fail("missing_field", "missing-information identity is incomplete");
    }
    const auto &missing_information_id =
        required_string(item, "missing_information_id");
    if (!missing_information_ids.insert(missing_information_id).second) {
      fail("duplicate_missing_information",
           "missing-information IDs must be unique");
    }
    missing_information_order.push_back(missing_information_id);
    if (!item.at("slot_id").is_null()) {
      const auto &slot_id = required_string(item, "slot_id");
      if (!slot_ids.contains(slot_id)) {
        fail("missing_information_slot",
             "missing information references an absent slot");
      }
    }
  }
  if (!std::is_sorted(missing_information_order.begin(),
                      missing_information_order.end())) {
    fail("invalid_contract_order",
         "missing information is not contract ordered");
  }
  std::unordered_set<std::string> limitation_ids;
  std::vector<std::string> limitation_order;
  for (const auto &limitation : required_array(root, "limitations")) {
    require_exact_members(limitation, {"limitation_id", "statement"});
    const auto &limitation_id = required_string(limitation, "limitation_id");
    if (!limitation_ids.insert(limitation_id).second) {
      fail("duplicate_limitation", "limitation IDs must be unique");
    }
    limitation_order.push_back(limitation_id);
    static_cast<void>(required_string(limitation, "statement"));
  }
  if (limitation_order.empty()) {
    fail("missing_limitation", "execution-component requires a limitation");
  }
  if (!std::is_sorted(limitation_order.begin(), limitation_order.end())) {
    fail("invalid_contract_order", "limitations are not contract ordered");
  }
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

std::string sha256_bytes(const std::string_view bytes) {
  return raw_sha256_identity(bytes);
}

StreamedFileSha256 sha256_file_chunks(
    const std::filesystem::path &path, const std::size_t chunkBytes,
    const std::function<void(std::string_view)> &consume) {
  if (chunkBytes == 0U) {
    fail("invalid_chunk_size", "SHA-256 file chunk size must be positive");
  }
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(path, status_error);
  if (status_error || status.type() == std::filesystem::file_type::not_found) {
    fail("file_read_failed", "unable to inspect file for SHA-256 hashing");
  }
  if (std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    fail("file_not_regular", "SHA-256 input is not a regular file");
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    fail("file_read_failed", "unable to open file for SHA-256 hashing");
  }

  picosha2::hash256_one_by_one hasher;
  std::vector<char> buffer(chunkBytes);
  std::uintmax_t byteLength = 0U;
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = stream.gcount();
    if (count > 0) {
      hasher.process(buffer.begin(), buffer.begin() + count);
      const auto size = static_cast<std::size_t>(count);
      byteLength += size;
      if (consume) consume(std::string_view(buffer.data(), size));
    }
  }
  if (!stream.eof()) {
    fail("file_read_failed", "unable to read file for SHA-256 hashing");
  }
  hasher.finish();
  return {"sha256:" + picosha2::get_hash_hex_string(hasher), byteLength};
}

std::string sha256_file(const std::filesystem::path &path) {
  return sha256_file_chunks(path, 64U * 1024U, {}).sha256;
}

std::string object_hash(const std::string_view canonical_bytes) {
  const auto verified = verify_canonical_bytes(canonical_bytes);
  return sha256_bytes(verified);
}

std::string verify_execution_component(
    const std::string_view stored_bytes,
    const std::string_view expected_object_hash, const Limits limits) {
  const auto verified = verify_canonical_bytes(stored_bytes, limits);
  const auto actual_hash = sha256_bytes(verified);
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
  validate_execution_component_graph(value, limits);
  return actual_hash;
}

} // namespace prometheus::integrity
