#include "cad_controller.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSignalSpy>

#include <cstdlib>

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
    return 0;
}
