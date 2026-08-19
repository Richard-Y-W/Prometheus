#include "execution_component_variant.hpp"

namespace prometheus {
namespace {

constexpr std::string_view packageMediaType =
    "application/vnd.prometheus.execution-component+json;version=2.0.0";
constexpr std::string_view packageSchemaId =
    "urn:prometheus:schema:execution-component:2.0.0";
constexpr std::string_view packageSchemaVersion = "2.0.0";

QString text(const std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QVariantList strings(const std::vector<std::string> &values) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const auto &value : values) {
    result.append(text(value));
  }
  return result;
}

} // namespace

QVariantMap
executionComponentVariant(const execution::PackageInspection &inspection) {
  return {
      {"package_hash", text(inspection.package_hash)},
      {"revision_id", text(inspection.revision_id)},
      {"component_id", text(inspection.component_id)},
      {"manufacturer", text(inspection.manufacturer)},
      {"part_number", text(inspection.part_number)},
      {"revision", text(inspection.revision)},
      {"component_class", text(inspection.component_class)},
      {"package_kind", text(inspection.package_kind)},
      {"capability_id", text(inspection.capability_id)},
      {"execution_readiness", text(inspection.execution_readiness)},
      {"limitations", strings(inspection.limitations)},
      {"blocked_reason", inspection.blocked_reason.has_value()
                             ? text(*inspection.blocked_reason)
                             : QString{}},
  };
}

run_store::StoredObjectReference
executionComponentReference(const execution::PackageInspection &inspection,
                            const std::size_t byteLength) {
  return {inspection.package_hash, static_cast<std::uint64_t>(byteLength),
          std::string(packageMediaType), std::string(packageSchemaId),
          std::string(packageSchemaVersion)};
}

} // namespace prometheus
