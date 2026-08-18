#include "cad_controller.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cstdlib>
#include <cstddef>
#include <string_view>

namespace {

[[noreturn]] void fail(const char* message)
{
    qCritical("FAILED: %s", message);
    std::exit(1);
}

void require(const bool condition, const char* message)
{
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    CadController controller;

    require(
        !controller.importStep("missing.step"),
        "sync import must fail when OCCT is disabled");
    require(
        controller.error() == "Open Cascade adapter is not enabled",
        "disabled adapter must be explicit");
    require(
        controller.parts().isEmpty(),
        "disabled adapter must not synthesize geometry");
    require(!controller.busy(), "disabled sync import must not remain busy");

    QSignalSpy completion(&controller, &CadController::importFinished);
    controller.importStepAsync("missing.step");
    QCoreApplication::processEvents();

    require(completion.count() == 1, "disabled async import must complete once");
    require(
        !completion.at(0).at(0).toBool(),
        "disabled async import must report false");
    require(
        controller.error() == "Open Cascade adapter is not enabled",
        "disabled async import must retain the explicit error");
    require(
        controller.parts().isEmpty(),
        "disabled async import must not synthesize geometry");
    require(!controller.busy(), "disabled async import must terminate");

    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory must be available");
    const auto cadPath = temporary.filePath("arm.step");
    const QByteArray cadBytes("stable CAD bytes");
    QFile cadFile(cadPath);
    require(cadFile.open(QIODevice::WriteOnly),
        "CAD hash fixture must open");
    require(cadFile.write(cadBytes) == cadBytes.size(),
        "CAD hash fixture must write completely");
    cadFile.close();
    const auto expected = QString::fromStdString(
        prometheus::integrity::sha256_bytes(std::string_view(
            cadBytes.constData(), static_cast<std::size_t>(cadBytes.size()))));
    controller.restoreCadState({{"name", "Arm"},
        {"cad_source", cadPath},
        {"resolved_cad_source", cadPath},
        {"assembly_artifact_hash", expected}});
    require(controller.error() == "Open Cascade adapter is not enabled",
        "restore must expose the disabled geometry adapter");
    require(controller.recheckAssemblyArtifact(expected),
        "matching CAD bytes must verify without OCCT");
    require(controller.error() == "Open Cascade adapter is not enabled",
        "successful hashing must not hide an unrelated adapter error");
    require(!controller.recheckAssemblyArtifact(
                "sha256:0000000000000000000000000000000000000000000000000000000000000000"),
        "mismatched CAD bytes must fail verification");
    require(controller.recheckAssemblyArtifact(expected),
        "restored matching bytes must verify again");
    require(controller.error() == "Open Cascade adapter is not enabled",
        "successful hashing clears only its own prior mismatch error");

    QSignalSpy bindingRequests(
        &controller, &CadController::componentBindingRequested);
    controller.bindComponentRevision(0, "revision-123");
    require(bindingRequests.count() == 0,
        "an out-of-range index must never request a component binding fetch");
    controller.reverifyComponentBinding(0);
    require(bindingRequests.count() == 0,
        "reverify on an out-of-range index must never request a fetch");
    controller.bindProvisionalCandidate(
        0, {{"id", "candidate-1"}, {"manufacturer", "Acme"}});
    require(controller.parts().isEmpty(),
        "binding calls on an out-of-range index must not synthesize parts");

    controller.failVerifiedComponentBinding(
        "missing-entity", "Verification failed.", "package_hash_mismatch");
    require(
        controller.error() ==
            "Verification failed. [package_hash_mismatch]",
        "a failed verified binding must surface its message and code");

    controller.applyVerifiedComponentBinding(
        "missing-entity",
        {{"revision_id", "revision-123"},
         {"manufacturer", "Acme"},
         {"part_number", "Widget"},
         {"revision", "1"},
         {"package_hash", "sha256:0000"}});
    require(
        controller.error() !=
            "Verification failed. [package_hash_mismatch]",
        "a verified binding applied against a missing CAD entity must "
        "report its own failure, not stale prior text");
    require(controller.parts().isEmpty(),
        "applying a verified binding must never synthesize a CAD part");
    return 0;
}
