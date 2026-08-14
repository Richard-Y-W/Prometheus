#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace prometheus::integrity {

struct Limits final {
  std::size_t raw_bytes{8U * 1024U * 1024U};
  std::size_t depth{64};
  std::size_t nodes{100000};
  std::size_t object_members{10000};
  std::size_t array_elements{10000};
  std::size_t string_bytes{1024U * 1024U};
};

class CanonicalJsonError final : public std::runtime_error {
public:
  CanonicalJsonError(std::string code, std::string message);
  [[nodiscard]] const std::string &code() const noexcept;

private:
  std::string code_;
};

[[nodiscard]] std::string canonicalize_json_bytes(
    std::string_view source, Limits limits = {});
[[nodiscard]] std::string verify_canonical_bytes(
    std::string_view source, Limits limits = {});
[[nodiscard]] std::string sha256_bytes(std::string_view bytes);
[[nodiscard]] std::string sha256_file(const std::filesystem::path &path);
[[nodiscard]] std::string object_hash(std::string_view canonical_bytes);
[[nodiscard]] std::string verify_execution_component(
    std::string_view stored_bytes, std::string_view expected_object_hash,
    Limits limits = {});

} // namespace prometheus::integrity
