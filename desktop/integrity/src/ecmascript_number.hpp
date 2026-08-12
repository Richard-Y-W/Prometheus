#pragma once

#include <string>

namespace prometheus::integrity::detail {

[[nodiscard]] std::string format_ecmascript_number(double value);

} // namespace prometheus::integrity::detail
