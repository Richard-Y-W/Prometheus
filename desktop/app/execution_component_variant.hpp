#pragma once

#include <prometheus/execution/package_consumer.hpp>
#include <prometheus/run_store/project_v2.hpp>

#include <QVariantMap>

#include <cstddef>

namespace prometheus {

// The one authoritative mapping from a hash-verified execution-component
// inspection to its QML-facing display representation. Every controller that
// turns verified package bytes into UI state must go through this function
// instead of re-deriving the same fields.
[[nodiscard]] QVariantMap
executionComponentVariant(const execution::PackageInspection &inspection);

// The one authoritative mapping from a hash-verified execution-component
// inspection to the stored-object reference run_store persistence uses to
// embed and later re-verify the exact bytes. Every controller that installs
// a package binding must go through this function instead of re-deriving
// the media type/schema identity.
[[nodiscard]] run_store::StoredObjectReference
executionComponentReference(const execution::PackageInspection &inspection,
                            std::size_t byteLength);

} // namespace prometheus
