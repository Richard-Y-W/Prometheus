import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    width: 1360
    height: 860
    visible: true
    title: "Prometheus — CAD Workspace"
    color: "#171b20"
    property color panel: "#20262c"
    property color line: "#35404a"
    property color text: "#dfe7ed"
    property color muted: "#91a0ab"
    property int selectedIndex: -1
    property var selectedIndices: []
    readonly property var serviceApi: serviceController
    readonly property var executionApi: executionController
    readonly property var projectApi: projectController
    readonly property var intakeApi: projectIntakeController
    readonly property var engineeringApi: engineeringController
    readonly property var structuralApi: typeof structuralController !== "undefined" ? structuralController : null
    readonly property bool hasSelection: selectedIndex >= 0 && selectedIndex < cadController.parts.length
    readonly property var selectedPart: hasSelection ? cadController.parts[selectedIndex] : null
    readonly property bool hasJointParts: engineeringController.jointConfigured && engineeringController.joint.source_index >= 0 && engineeringController.joint.target_index >= 0 && engineeringController.joint.source_index < cadController.parts.length && engineeringController.joint.target_index < cadController.parts.length
    // Phase 6 checkpoint 7: on-demand reads of the selected part's full
    // PackageBinding/JointBinding supersession chains. componentBindingController.busy
    // and engineeringController.joint are read purely to give these bindings something
    // to re-evaluate on: a bind's async busy->!busy toggle and a (re)defined joint's
    // synchronous property change respectively, at exactly the moments the chain changes.
    readonly property var componentBindingHistory: (hasSelection && !componentBindingController.busy) ? componentBindingController.bindingHistory(selectedPart.persistentId) : []
    readonly property var jointHistoryForSelection: (hasSelection && engineeringController.joint !== undefined) ? engineeringController.jointHistory(selectedPart.persistentId) : []
    property bool xrayMode: false
    property bool boundsMode: false
    property var measurement: ({})
    property bool perspectiveMode: true
    property real panX: 0
    property real panY: 0
    property real orthoZoom: 1
    property string transformMode: "move"
    property string transformFrame: "world"
    property real moveIncrement: 0.005
    property real rotateIncrement: 5
    property bool snapEnabled: true
    property real orbitX: -24
    property real orbitY: -38
    property real cameraDistance: Math.max(0.25, cadController.sceneDiameter * 2.0)
    function fitView() {
        orbitX = -24;
        orbitY = -38;
        panX = 0;
        panY = 0;
        orthoZoom = 1;
        cameraDistance = Math.max(0.05, cadController.sceneDiameter * 2.0);
    }
    function setView(x, y, orthographic) {
        orbitX = x;
        orbitY = y;
        panX = 0;
        panY = 0;
        orthoZoom = 1;
        cameraDistance = Math.max(0.05, cadController.sceneDiameter * 2.0);
        if (orthographic !== undefined)
            perspectiveMode = !orthographic;
    }
    function nudgeSelection(axis, direction) {
        if (!hasSelection || cadController.geometryBusy)
            return;
        const p = selectedPart;
        const v = transformFrame === "local" ? cadController.localAxisDirection(selectedIndex, axis) : ({
                x: axis === "X" ? 1 : 0,
                y: axis === "Y" ? 1 : 0,
                z: axis === "Z" ? 1 : 0
            });
        if (transformMode === "move" && selectedIndices.length > 1) {
            groupTranslate(direction * moveIncrement * v.x, direction * moveIncrement * v.y, direction * moveIncrement * v.z);
            return;
        }
        let x = p.translationX, y = p.translationY, z = p.translationZ, rx = p.rotationX, ry = p.rotationY, rz = p.rotationZ;
        if (transformMode === "move") {
            x += direction * moveIncrement * v.x;
            y += direction * moveIncrement * v.y;
            z += direction * moveIncrement * v.z;
        } else if (transformFrame === "local") {
            const r = cadController.composeLocalRotation(rx, ry, rz, axis, direction * rotateIncrement);
            rx = r.rx;
            ry = r.ry;
            rz = r.rz;
        } else {
            if (axis === "X")
                rx += direction * rotateIncrement;
            if (axis === "Y")
                ry += direction * rotateIncrement;
            if (axis === "Z")
                rz += direction * rotateIncrement;
        }
        cadController.setPartPlacement(selectedIndex, x, y, z, rx, ry, rz);
    }
    function snapped(value, step) {
        return Math.round(value / step) * step;
    }
    function isSelected(index) {
        return selectedIndices.length > 0 ? selectedIndices.indexOf(index) >= 0 : index === selectedIndex;
    }
    function selectOnly(index) {
        selectedIndex = index;
        selectedIndices = index >= 0 ? [index] : [];
    }
    function toggleSelection(index) {
        let next = selectedIndices.slice();
        const at = next.indexOf(index);
        if (at >= 0)
            next.splice(at, 1);
        else
            next.push(index);
        selectedIndices = next;
        selectedIndex = next.length > 0 ? next[next.length - 1] : -1;
    }
    function selectedIndexValues() {
        return selectedIndices.length > 0 ? selectedIndices : [selectedIndex];
    }
    function groupTranslate(dx, dy, dz) {
        if (!hasSelection)
            return;
        if (cadController.beginGroupTranslationPreview(selectedIndexValues())) {
            cadController.previewGroupTranslation(dx, dy, dz);
            cadController.commitPlacementPreview();
        }
    }
    Component.onCompleted: {
        if (String(startupProjectFolder) !== "")
            projectIntakeController.scanFolder(startupProjectFolder);
        else if (startupStepPath !== "")
            cadController.importStepAsync(startupStepPath);
        if (demoCadInspect && cadController.parts.length > 1) {
            boundsMode = true;
            perspectiveMode = false;
            selectedIndex = 1;
        }
        if (demoPlacement && cadController.parts.length > 2) {
            boundsMode = true;
            selectedIndex = 2;
            cadController.setPartPlacement(2, 0, 0.08, 0, 0, 0, 30);
        }
        if (demoEngineering && cadController.parts.length > 2) {
            engineeringController.defineRevoluteJoint(1, 2, "Z", 0, 90, cadController.parts[1].centerX, cadController.parts[1].centerY, cadController.parts[1].centerZ, cadController.parts[1].persistentId, cadController.parts[2].persistentId);
            cadController.runJointSweepAsync(2, 1, engineeringController.joint.pivot_x, engineeringController.joint.pivot_y, engineeringController.joint.pivot_z, "Z", 0, 90);
        }
        if (typeof demoStructural !== "undefined" && demoStructural)
            structuralWorkflowDialog.open();
    }
    header: Column {
        width: parent.width
        Rectangle {
            width: parent.width
            height: 29
            color: "#171b20"
            Row {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 14
                spacing: 24
                Repeater {
                    model: ["File", "Edit", "View", "Assembly", "Components", "Test", "Tools", "Help"]
                    Label {
                        text: modelData
                        color: window.text
                        font.pixelSize: 12
                    }
                }
            }
        }
        Rectangle {
            width: parent.width
            height: 52
            color: "#242b31"
            border.color: line
            RowLayout {
                anchors.fill: parent
                anchors.margins: 7
                spacing: 7
                Label {
                    text: "P"
                    color: "white"
                    font.bold: true
                    font.pixelSize: 20
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    background: Rectangle {
                        color: "#d7643d"
                    }
                    Layout.preferredWidth: 34
                    Layout.fillHeight: true
                }
                Label {
                    text: "PROMETHEUS"
                    color: window.text
                    font.bold: true
                    Layout.rightMargin: 18
                }
                Button {
                    objectName: "openFolderButton"
                    text: "Open Folder"
                    onClicked: folderDialog.open()
                    enabled: !cadController.busy && !projectIntakeController.busy
                }
                Button {
                    objectName: "fileActionsButton"
                    text: "File Actions ▾"
                    onClicked: fileActionsMenu.open()
                    Menu {
                        id: fileActionsMenu
                        y: parent.height
                        MenuItem {
                            text: "Import individual STEP…"
                            enabled: !cadController.busy
                            onTriggered: stepDialog.open()
                        }
                        MenuItem {
                            text: "Open Prometheus project…"
                            enabled: !cadController.busy
                            onTriggered: openDialog.open()
                        }
                        MenuItem {
                            text: "Recover damaged Prometheus project…"
                            enabled: !cadController.busy
                            onTriggered: recoveryDialog.open()
                        }
                        MenuItem {
                            text: projectController.bundleBusy ? "Exporting portable bundle…" : "Export portable project bundle…"
                            enabled: projectController.currentProjectPath !== "" && !projectController.bundleBusy
                            onTriggered: bundleFolderDialog.open()
                        }
                        MenuItem {
                            text: projectController.bundleBusy ? "Portable bundle operation in progress…" : "Restore portable project bundle…"
                            enabled: !projectController.bundleBusy
                            onTriggered: restoreBundleSourceDialog.open()
                        }
                        MenuItem {
                            text: projectIntakeController.rootPath === "" ? "Project inventory unavailable" : "Project inventory (" + projectIntakeController.totalCount + ")"
                            enabled: projectIntakeController.rootPath !== ""
                            onTriggered: inventoryDialog.open()
                        }
                    }
                }
                Button {
                    text: projectController.saveAsRequired ? "Save As v2" : "Save Project"
                    onClicked: projectController.saveAsRequired ? saveDialog.open() : projectController.saveCurrentProject()
                    enabled: (cadController.parts.length > 0 || projectController.currentProjectPath !== "") && !cadController.busy
                }
                Button {
                    objectName: "screenResultsButton"
                    text: cadController.sweepBusy ? "Checking…" : hasJointParts ? "Check Motion" : "Screen Results"
                    highlighted: true
                    enabled: cadController.parts.length > 0 && !cadController.sweepBusy && !cadController.geometryBusy
                    ToolTip.visible: hovered
                    ToolTip.text: hasJointParts ? "Runs the reviewed sampled joint sweep and refreshes the mechanical screen." : "Shows evaluated static geometry and questions that remain unknown."
                    onClicked: {
                        if (hasJointParts) {
                            const source = cadController.parts[engineeringController.joint.source_index];
                            cadController.runJointSweepAsync(engineeringController.joint.target_index, engineeringController.joint.source_index, engineeringController.joint.pivot_x + source.translationX, engineeringController.joint.pivot_y + source.translationY, engineeringController.joint.pivot_z + source.translationZ, engineeringController.joint.axis, engineeringController.joint.minimum_deg, engineeringController.joint.maximum_deg);
                        } else {
                            engineeringController.runGeometryChecks(cadController.interferences, [], false, !cadController.collisionDeferred);
                            resultsDialog.open();
                        }
                    }
                }
                Button {
                    text: "Add Component"
                    enabled: hasSelection
                    onClicked: {
                        executionController.setPendingCadEntityId(selectedPart.persistentId);
                        serviceController.reset();
                        componentDialog.open();
                    }
                }
                Button {
                    text: "Motor Analysis"
                    enabled: projectController.currentProjectPath !== "" || cadController.parts.length > 0
                    onClicked: motorWorkflowDialog.open()
                }
                Button {
                    objectName: "structuralAnalysisButton"
                    text: "Structural"
                    enabled: window.structuralApi !== null
                    onClicked: structuralWorkflowDialog.open()
                }
                Button {
                    text: "Transform"
                    enabled: hasSelection && !cadController.geometryBusy
                    onClicked: {
                        const p = selectedPart;
                        moveX.text = String(p.translationX);
                        moveY.text = String(p.translationY);
                        moveZ.text = String(p.translationZ);
                        rotateX.text = String(p.rotationX);
                        rotateY.text = String(p.rotationY);
                        rotateZ.text = String(p.rotationZ);
                        moveDialog.open();
                    }
                }
                Button {
                    text: transformMode === "move" ? "Move [W]" : "Rotate [E]"
                    enabled: hasSelection
                    onClicked: transformMode = transformMode === "move" ? "rotate" : "move"
                    ToolTip.visible: hovered
                    ToolTip.text: "Toggle the viewport axis manipulator"
                }
                Button {
                    text: transformFrame === "world" ? "World" : "Local"
                    enabled: hasSelection
                    onClicked: transformFrame = transformFrame === "world" ? "local" : "world"
                    ToolTip.visible: hovered
                    ToolTip.text: "Toggle world/local transform coordinates"
                }
                Button {
                    text: snapEnabled ? "Snap ✓" : "Snap"
                    checkable: true
                    checked: snapEnabled
                    onClicked: snapEnabled = checked
                    ToolTip.visible: hovered
                    ToolTip.text: "Grid/angular snap; hold Alt while dragging to bypass"
                }
                Button {
                    text: "↶"
                    enabled: cadController.canUndo && !cadController.geometryBusy
                    ToolTip.visible: hovered
                    ToolTip.text: "Undo placement (Ctrl+Z)"
                    onClicked: cadController.undoPlacement()
                }
                Button {
                    text: "↷"
                    enabled: cadController.canRedo && !cadController.geometryBusy
                    ToolTip.visible: hovered
                    ToolTip.text: "Redo placement (Ctrl+Y)"
                    onClicked: cadController.redoPlacement()
                }
                Button {
                    text: "Measure"
                    enabled: cadController.parts.length > 1
                    onClicked: measureDialog.open()
                }
                Button {
                    text: "Snap Mate"
                    enabled: cadController.parts.length > 1 && !cadController.geometryBusy
                    onClicked: mateDialog.open()
                }
                Button {
                    text: xrayMode ? "X-Ray ✓" : "X-Ray"
                    checkable: true
                    checked: xrayMode
                    onClicked: xrayMode = checked
                }
                Button {
                    text: boundsMode ? "Bounds ✓" : "Bounds"
                    checkable: true
                    checked: boundsMode
                    onClicked: boundsMode = checked
                }
                Button {
                    text: "Overlaps"
                    enabled: cadController.interferences.length > 0
                    onClicked: connectionDialog.open()
                }
                Button {
                    text: "Connections" + (cadController.connections.length > 0 ? " (" + cadController.connections.length + ")" : "")
                    enabled: cadController.connections.length > 0
                    onClicked: semanticConnectionsDialog.open()
                }
                Button {
                    text: engineeringController.jointConfigured ? "Joint ✓" : "Define Joint"
                    enabled: cadController.parts.length > 1
                    onClicked: jointDialog.open()
                }
                Item {
                    Layout.fillWidth: true
                }
            }
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 1
        Rectangle {
            Layout.minimumWidth: 245
            Layout.maximumWidth: 245
            Layout.fillHeight: true
            color: panel
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                Label {
                    text: "ASSEMBLY"
                    color: muted
                    font.bold: true
                    font.pixelSize: 11
                    Layout.margins: 13
                }
                Label {
                    visible: cadController.parts.length === 0
                    text: "Import a STEP assembly to begin."
                    color: muted
                    wrapMode: Text.WordWrap
                    Layout.margins: 14
                    Layout.fillWidth: true
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: cadController.parts
                    clip: true
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 48
                        color: index === selectedIndex ? "#2e424f" : "transparent"
                        border.color: index === selectedIndex ? "#3d9bd6" : "transparent"
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 14
                            spacing: 2
                            Label {
                                text: "◇  " + modelData.name
                                color: window.text
                            }
                            Label {
                                text: modelData.componentLabel !== "" ? modelData.componentLabel : modelData.persistentId
                                color: modelData.componentLabel === "" ? muted
                                       : !modelData.componentVerified ? "#e0ac62"
                                       : modelData.componentSupersededByRevisionId !== "" ? "#e87972"
                                       : "#70c99a"
                                font.pixelSize: 10
                            }
                        }
                        Button {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: 7
                            text: modelData.visible ? "●" : "○"
                            flat: true
                            onClicked: cadController.toggleVisible(index)
                        }
                        MouseArea {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.rightMargin: 42
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            onClicked: selectedIndex = index
                            onDoubleClicked: cadController.isolate(index)
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#151a1f"
            View3D {
                id: view
                anchors.fill: parent
                camera: perspectiveMode ? perspectiveCamera : orthographicCamera
                environment: SceneEnvironment {
                    clearColor: "#111820"
                    backgroundMode: SceneEnvironment.Color
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.VeryHigh
                    tonemapMode: SceneEnvironment.TonemapModeFilmic
                }
                Node {
                    id: cameraRig
                    position: Qt.vector3d(panX, panY, 0)
                    eulerRotation.x: orbitX
                    eulerRotation.y: orbitY
                    PerspectiveCamera {
                        id: perspectiveCamera
                        z: cameraDistance
                        clipNear: Math.max(0.00001, cadController.sceneDiameter * 0.0001)
                        clipFar: 100
                    }
                    OrthographicCamera {
                        id: orthographicCamera
                        z: cameraDistance
                        clipNear: 0.00001
                        clipFar: 100
                        horizontalMagnification: orthoZoom * Math.max(1, Math.min(view.width, view.height)) / (Math.max(cadController.sceneDiameter, 0.01) * 1.65)
                        verticalMagnification: horizontalMagnification
                    }
                }
                DirectionalLight {
                    eulerRotation.x: -38
                    eulerRotation.y: -32
                    brightness: 1.35
                    castsShadow: true
                    shadowFactor: 45
                    shadowMapQuality: Light.ShadowMapQualityHigh
                }
                DirectionalLight {
                    eulerRotation.x: 35
                    eulerRotation.y: 145
                    brightness: 0.45
                    color: "#9fc8e7"
                }
                Node {
                    id: assemblyRoot
                    eulerRotation.x: -90
                    position: Qt.vector3d(-cadController.centerX, -cadController.centerY, -cadController.centerZ)
                    Node {
                        id: gridRoot
                        position: Qt.vector3d(cadController.centerX, cadController.centerY, cadController.sceneMinZ - Math.max(cadController.sceneDiameter * 0.002, 0.001))
                        property real spacing: cadController.sceneDiameter / 10
                        property real extent: cadController.sceneDiameter * 1.35
                        property real thickness: Math.max(cadController.sceneDiameter * 0.0012, 0.00035)
                        Repeater3D {
                            model: 21
                            Model {
                                source: "#Cube"
                                position: Qt.vector3d((index - 10) * gridRoot.spacing, 0, 0)
                                scale: Qt.vector3d(gridRoot.thickness / 100, gridRoot.extent / 100, gridRoot.thickness / 100)
                                materials: [
                                    PrincipledMaterial {
                                        baseColor: index === 10 ? "#526878" : "#26343e"
                                        lighting: PrincipledMaterial.NoLighting
                                    }
                                ]
                            }
                        }
                        Repeater3D {
                            model: 21
                            Model {
                                source: "#Cube"
                                position: Qt.vector3d(0, (index - 10) * gridRoot.spacing, 0)
                                scale: Qt.vector3d(gridRoot.extent / 100, gridRoot.thickness / 100, gridRoot.thickness / 100)
                                materials: [
                                    PrincipledMaterial {
                                        baseColor: index === 10 ? "#526878" : "#26343e"
                                        lighting: PrincipledMaterial.NoLighting
                                    }
                                ]
                            }
                        }
                    }
                    Repeater3D {
                        model: cadController.parts
                        Node {
                            position: Qt.vector3d(modelData.centerX + modelData.translationX, modelData.centerY + modelData.translationY, modelData.centerZ + modelData.translationZ)
                            eulerRotation.z: modelData.rotationZ
                            Node {
                                eulerRotation.y: modelData.rotationY
                                Node {
                                    eulerRotation.x: modelData.rotationX
                                    Model {
                                        property int partIndex: index
                                        position: Qt.vector3d(-modelData.centerX, -modelData.centerY, -modelData.centerZ)
                                        geometry: modelData.geometry
                                        visible: modelData.visible
                                        opacity: xrayMode ? 0.28 : 1
                                        pickable: true
                                        castsShadows: !xrayMode
                                        receivesShadows: true
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: isSelected(index) ? "#36a5e3" : "#8b9dab"
                                                roughness: 0.46
                                                metalness: 0.08
                                                lighting: PrincipledMaterial.FragmentLighting
                                            }
                                        ]
                                    }
                                }
                            }
                        }
                    }
                    Repeater3D {
                        model: cadController.parts
                        Node {
                            visible: boundsMode && modelData.visible
                            position: Qt.vector3d(modelData.centerX + modelData.translationX, modelData.centerY + modelData.translationY, modelData.centerZ + modelData.translationZ)
                            eulerRotation.z: modelData.rotationZ
                            Node {
                                eulerRotation.y: modelData.rotationY
                                Node {
                                    eulerRotation.x: modelData.rotationX
                                    Model {
                                        source: "#Cube"
                                        scale: Qt.vector3d(Math.max(modelData.sizeX, 0.0001) / 100, Math.max(modelData.sizeY, 0.0001) / 100, Math.max(modelData.sizeZ, 0.0001) / 100)
                                        opacity: 0.18
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: isSelected(index) ? "#32b3ff" : "#d7a94a"
                                                lighting: PrincipledMaterial.NoLighting
                                            }
                                        ]
                                    }
                                }
                            }
                        }
                    }
                    Model {
                        visible: hasJointParts
                        source: "#Cylinder"
                        position: hasJointParts ? Qt.vector3d(engineeringController.joint.pivot_x + cadController.parts[engineeringController.joint.source_index].translationX, engineeringController.joint.pivot_y + cadController.parts[engineeringController.joint.source_index].translationY, engineeringController.joint.pivot_z + cadController.parts[engineeringController.joint.source_index].translationZ) : Qt.vector3d(0, 0, 0)
                        scale: Qt.vector3d(0.00005, Math.max(cadController.sceneDiameter, 0.1) / 100, 0.00005)
                        eulerRotation.x: engineeringController.joint.axis === "Z" ? 90 : 0
                        eulerRotation.z: engineeringController.joint.axis === "X" ? 90 : 0
                        materials: [
                            PrincipledMaterial {
                                baseColor: "#45b7e8"
                                lighting: PrincipledMaterial.NoLighting
                            }
                        ]
                    }
                    Node {
                        id: transformGizmo
                        visible: hasSelection && !cadController.geometryBusy
                        property real span: Math.max(cadController.sceneDiameter * 0.18, 0.035)
                        position: hasSelection ? Qt.vector3d(selectedPart.centerX + selectedPart.translationX, selectedPart.centerY + selectedPart.translationY, selectedPart.centerZ + selectedPart.translationZ) : Qt.vector3d(0, 0, 0)
                        Node {
                            eulerRotation.z: transformFrame === "local" && hasSelection ? selectedPart.rotationZ : 0
                            Node {
                                eulerRotation.y: transformFrame === "local" && hasSelection ? selectedPart.rotationY : 0
                                Node {
                                    id: gizmoAxes
                                    eulerRotation.x: transformFrame === "local" && hasSelection ? selectedPart.rotationX : 0
                                    Model {
                                        property string gizmoAxis: "X"
                                        source: "#Cylinder"
                                        position: Qt.vector3d(transformGizmo.span / 2, 0, 0)
                                        eulerRotation.z: 90
                                        scale: Qt.vector3d(0.000045, transformGizmo.span / 100, 0.000045)
                                        pickable: true
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: "#ef655e"
                                                lighting: PrincipledMaterial.NoLighting
                                            }
                                        ]
                                    }
                                    Model {
                                        property string gizmoAxis: "Y"
                                        source: "#Cylinder"
                                        position: Qt.vector3d(0, transformGizmo.span / 2, 0)
                                        scale: Qt.vector3d(0.000045, transformGizmo.span / 100, 0.000045)
                                        pickable: true
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: "#65cf87"
                                                lighting: PrincipledMaterial.NoLighting
                                            }
                                        ]
                                    }
                                    Model {
                                        property string gizmoAxis: "Z"
                                        source: "#Cylinder"
                                        position: Qt.vector3d(0, 0, transformGizmo.span / 2)
                                        eulerRotation.x: 90
                                        scale: Qt.vector3d(0.000045, transformGizmo.span / 100, 0.000045)
                                        pickable: true
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: "#55a8ec"
                                                lighting: PrincipledMaterial.NoLighting
                                            }
                                        ]
                                    }
                                    Model {
                                        property string gizmoAxis: "X"
                                        source: "#Sphere"
                                        position: Qt.vector3d(transformGizmo.span, 0, 0)
                                        scale: Qt.vector3d(0.00012, 0.00012, 0.00012)
                                        pickable: true
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: "#ef655e"
                                                lighting: PrincipledMaterial.NoLighting
                                            }
                                        ]
                                    }
                                    Model {
                                        property string gizmoAxis: "Y"
                                        source: "#Sphere"
                                        position: Qt.vector3d(0, transformGizmo.span, 0)
                                        scale: Qt.vector3d(0.00012, 0.00012, 0.00012)
                                        pickable: true
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: "#65cf87"
                                                lighting: PrincipledMaterial.NoLighting
                                            }
                                        ]
                                    }
                                    Model {
                                        property string gizmoAxis: "Z"
                                        source: "#Sphere"
                                        position: Qt.vector3d(0, 0, transformGizmo.span)
                                        scale: Qt.vector3d(0.00012, 0.00012, 0.00012)
                                        pickable: true
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: "#55a8ec"
                                                lighting: PrincipledMaterial.NoLighting
                                            }
                                        ]
                                    }
                                }
                            }
                        }
                    }
                }
            }
            MouseArea {
                id: viewInput
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                property real lastX
                property real lastY
                property real pressX
                property real pressY
                property real cursorX
                property real cursorY
                property real projectionDepth: 0
                property string draggingAxis: ""
                property string interactionMode: ""
                property bool groupPreview: false
                property bool dragged: false
                property bool suppressClick: false
                property var dragStart: ({})
                property var pressWorld: Qt.vector3d(0, 0, 0)
                property string transientValue: ""
                function projectedFraction(axis, dx, dy) {
                    const origin = view.mapFrom3DScene(gizmoAxes.mapPositionToScene(Qt.vector3d(0, 0, 0)));
                    let local = axis === "X" ? Qt.vector3d(transformGizmo.span, 0, 0) : axis === "Y" ? Qt.vector3d(0, transformGizmo.span, 0) : Qt.vector3d(0, 0, transformGizmo.span);
                    const endpoint = view.mapFrom3DScene(gizmoAxes.mapPositionToScene(local));
                    const vx = endpoint.x - origin.x, vy = endpoint.y - origin.y;
                    const length2 = vx * vx + vy * vy;
                    return length2 < 16 ? 0 : (dx * vx + dy * vy) / length2;
                }
                function beginSelectedPreview() {
                    groupPreview = transformMode === "move" && selectedIndices.length > 1;
                    return groupPreview ? cadController.beginGroupTranslationPreview(selectedIndexValues()) : cadController.beginPlacementPreview(selectedIndex);
                }
                function selectMarquee() {
                    const left = Math.min(pressX, cursorX), right = Math.max(pressX, cursorX), top = Math.min(pressY, cursorY), bottom = Math.max(pressY, cursorY);
                    let found = [];
                    for (let i = 0; i < cadController.parts.length; i++) {
                        const p = cadController.parts[i];
                        if (!p.visible)
                            continue;
                        const scene = assemblyRoot.mapPositionToScene(Qt.vector3d(p.centerX + p.translationX, p.centerY + p.translationY, p.centerZ + p.translationZ));
                        const screen = view.mapFrom3DScene(scene);
                        if (screen.x >= left && screen.x <= right && screen.y >= top && screen.y <= bottom)
                            found.push(i);
                    }
                    selectedIndices = found;
                    selectedIndex = found.length > 0 ? found[found.length - 1] : -1;
                }
                onPressed: function (mouse) {
                    lastX = mouse.x;
                    lastY = mouse.y;
                    pressX = mouse.x;
                    pressY = mouse.y;
                    cursorX = mouse.x;
                    cursorY = mouse.y;
                    dragged = false;
                    if (mouse.button !== Qt.LeftButton)
                        return;
                    const hit = view.pick(mouse.x, mouse.y);
                    if (hit.objectHit && hit.objectHit.gizmoAxis !== undefined && beginSelectedPreview()) {
                        interactionMode = "axis";
                        draggingAxis = hit.objectHit.gizmoAxis;
                        const p = cadController.parts[selectedIndex];
                        const v = transformFrame === "local" ? cadController.localAxisDirection(selectedIndex, draggingAxis) : ({
                                x: draggingAxis === "X" ? 1 : 0,
                                y: draggingAxis === "Y" ? 1 : 0,
                                z: draggingAxis === "Z" ? 1 : 0
                            });
                        dragStart = {
                            x: p.translationX,
                            y: p.translationY,
                            z: p.translationZ,
                            rx: p.rotationX,
                            ry: p.rotationY,
                            rz: p.rotationZ,
                            dx: v.x,
                            dy: v.y,
                            dz: v.z
                        };
                        return;
                    }
                    if (hit.objectHit && hit.objectHit.partIndex !== undefined) {
                        const index = hit.objectHit.partIndex;
                        if (mouse.modifiers & Qt.ControlModifier)
                            toggleSelection(index);
                        else if (!isSelected(index))
                            selectOnly(index);
                        if (hasSelection && cadController.beginGroupTranslationPreview(selectedIndexValues())) {
                            interactionMode = "direct";
                            groupPreview = true;
                            const p = cadController.parts[selectedIndex];
                            const projected = view.mapFrom3DScene(assemblyRoot.mapPositionToScene(Qt.vector3d(p.centerX + p.translationX, p.centerY + p.translationY, p.centerZ + p.translationZ)));
                            projectionDepth = projected.z;
                            pressWorld = assemblyRoot.mapPositionFromScene(view.mapTo3DScene(Qt.vector3d(mouse.x, mouse.y, projectionDepth)));
                        }
                        return;
                    }
                    interactionMode = "marquee";
                    if (!(mouse.modifiers & Qt.ControlModifier))
                        selectOnly(-1);
                }
                onPositionChanged: function (mouse) {
                    cursorX = mouse.x;
                    cursorY = mouse.y;
                    if (interactionMode === "marquee" && (mouse.buttons & Qt.LeftButton)) {
                        if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 3)
                            dragged = true;
                        return;
                    }
                    if (interactionMode === "direct" && (mouse.buttons & Qt.LeftButton)) {
                        const dx = mouse.x - pressX, dy = mouse.y - pressY;
                        if (Math.abs(dx) + Math.abs(dy) > 3)
                            dragged = true;
                        if (!dragged)
                            return;
                        const current = assemblyRoot.mapPositionFromScene(view.mapTo3DScene(Qt.vector3d(mouse.x, mouse.y, projectionDepth)));
                        let tx = current.x - pressWorld.x, ty = current.y - pressWorld.y, tz = current.z - pressWorld.z;
                        if (snapEnabled && !(mouse.modifiers & Qt.AltModifier)) {
                            tx = snapped(tx, moveIncrement);
                            ty = snapped(ty, moveIncrement);
                            tz = snapped(tz, moveIncrement);
                        }
                        cadController.previewGroupTranslation(tx, ty, tz);
                        transientValue = "FREE DRAG";
                        return;
                    }
                    if (draggingAxis !== "" && (mouse.buttons & Qt.LeftButton)) {
                        const dx = mouse.x - pressX, dy = mouse.y - pressY;
                        if (Math.abs(dx) + Math.abs(dy) > 3)
                            dragged = true;
                        if (!dragged)
                            return;
                        const fraction = projectedFraction(draggingAxis, dx, dy);
                        const useSnap = snapEnabled && !(mouse.modifiers & Qt.AltModifier);
                        let x = dragStart.x, y = dragStart.y, z = dragStart.z, rx = dragStart.rx, ry = dragStart.ry, rz = dragStart.rz;
                        if (transformMode === "move") {
                            let delta = fraction * transformGizmo.span;
                            if (useSnap)
                                delta = snapped(delta, moveIncrement);
                            if (groupPreview)
                                cadController.previewGroupTranslation(delta * dragStart.dx, delta * dragStart.dy, delta * dragStart.dz);
                            else {
                                x += delta * dragStart.dx;
                                y += delta * dragStart.dy;
                                z += delta * dragStart.dz;
                            }
                            transientValue = draggingAxis + "  " + (delta * 1000).toFixed(useSnap ? 0 : 2) + " mm";
                        } else {
                            let delta = fraction * 90;
                            if (useSnap)
                                delta = snapped(delta, rotateIncrement);
                            if (transformFrame === "local") {
                                const r = cadController.composeLocalRotation(dragStart.rx, dragStart.ry, dragStart.rz, draggingAxis, delta);
                                rx = r.rx;
                                ry = r.ry;
                                rz = r.rz;
                            } else {
                                if (draggingAxis === "X")
                                    rx += delta;
                                if (draggingAxis === "Y")
                                    ry += delta;
                                if (draggingAxis === "Z")
                                    rz += delta;
                            }
                            transientValue = draggingAxis + "  " + delta.toFixed(useSnap ? 0 : 2) + "°";
                        }
                        if (!groupPreview)
                            cadController.previewPartPlacement(selectedIndex, x, y, z, rx, ry, rz);
                        return;
                    }
                    if ((mouse.buttons & Qt.RightButton) || ((mouse.buttons & Qt.MiddleButton) && (mouse.modifiers & Qt.ShiftModifier))) {
                        const dx = mouse.x - lastX, dy = mouse.y - lastY;
                        panX += dx * cameraDistance * 0.0015;
                        panY -= dy * cameraDistance * 0.0015;
                        lastX = mouse.x;
                        lastY = mouse.y;
                        return;
                    }
                    if (mouse.buttons & Qt.MiddleButton) {
                        const dx = mouse.x - lastX, dy = mouse.y - lastY;
                        orbitY += dx * 0.35;
                        orbitX = Math.max(-89, Math.min(89, orbitX + dy * 0.35));
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }
                }
                onReleased: function (mouse) {
                    if (mouse.button !== Qt.LeftButton)
                        return;
                    if (interactionMode === "marquee") {
                        if (dragged)
                            selectMarquee();
                    } else if (interactionMode === "axis") {
                        if (dragged)
                            cadController.commitPlacementPreview();
                        else {
                            cadController.cancelPlacementPreview();
                            nudgeSelection(draggingAxis, (mouse.modifiers & Qt.ShiftModifier) ? -1 : 1);
                        }
                    } else if (interactionMode === "direct") {
                        if (dragged)
                            cadController.commitPlacementPreview();
                        else
                            cadController.cancelPlacementPreview();
                    }
                    interactionMode = "";
                    draggingAxis = "";
                    groupPreview = false;
                    transientValue = "";
                }
                onCanceled: {
                    if (interactionMode === "axis" || interactionMode === "direct")
                        cadController.cancelPlacementPreview();
                    interactionMode = "";
                    draggingAxis = "";
                    groupPreview = false;
                    dragged = false;
                    transientValue = "";
                }
                onWheel: function (wheel) {
                    if (perspectiveMode)
                        cameraDistance = Math.max(cadController.sceneDiameter * 0.06, Math.min(cadController.sceneDiameter * 20, cameraDistance * (wheel.angleDelta.y > 0 ? 0.82 : 1.20)));
                    else
                        orthoZoom = Math.max(0.15, Math.min(30, orthoZoom * (wheel.angleDelta.y > 0 ? 1.18 : 0.84)));
                }
            }
            Rectangle {
                visible: viewInput.dragged && (viewInput.interactionMode === "axis" || viewInput.interactionMode === "direct")
                x: Math.min(view.width - width - 8, viewInput.cursorX + 16)
                y: Math.min(view.height - height - 8, viewInput.cursorY + 16)
                width: 145
                height: 34
                radius: 4
                color: "#202830ee"
                border.color: viewInput.interactionMode === "direct" ? "#8ca7d8" : viewInput.draggingAxis === "X" ? "#ef655e" : viewInput.draggingAxis === "Y" ? "#65cf87" : "#55a8ec"
                Label {
                    anchors.centerIn: parent
                    text: viewInput.transientValue
                    color: window.text
                    font.bold: true
                }
            }
            Rectangle {
                visible: viewInput.interactionMode === "marquee" && viewInput.dragged
                x: Math.min(viewInput.pressX, viewInput.cursorX)
                y: Math.min(viewInput.pressY, viewInput.cursorY)
                width: Math.abs(viewInput.cursorX - viewInput.pressX)
                height: Math.abs(viewInput.cursorY - viewInput.pressY)
                color: "#369ed322"
                border.color: "#55b8eb"
                border.width: 1
            }
            Row {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 5
                Button {
                    text: "ISO"
                    onClicked: setView(-24, -38, false)
                }
                Button {
                    text: "TOP"
                    onClicked: setView(-89, 0, true)
                }
                Button {
                    text: "FRONT"
                    onClicked: setView(0, 0, true)
                }
                Button {
                    text: perspectiveMode ? "PERSP" : "ORTHO"
                    onClicked: perspectiveMode = !perspectiveMode
                }
                Button {
                    text: "Show all"
                    onClicked: cadController.showAll()
                }
                Button {
                    text: "Fit"
                    onClicked: fitView()
                }
            }
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 14
                anchors.topMargin: 52
                width: 66
                height: 66
                color: "#26313a"
                border.color: "#60727f"
                radius: 4
                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    Label {
                        text: Math.abs(orbitX) > 80 ? "TOP" : Math.abs(orbitY) < 5 ? "FRONT" : "ISO"
                        color: "#a9d5ee"
                        font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Label {
                        text: perspectiveMode ? "Perspective" : "Orthographic"
                        color: muted
                        font.pixelSize: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: setView(-24, -38)
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 12
                width: 750
                height: 28
                color: "#252c32cc"
                border.color: line
                Label {
                    anchors.centerIn: parent
                    text: "Drag part: move  •  Empty drag: box select  •  Ctrl+click: add/remove  •  WASD/QE: camera  •  T/R: move/rotate"
                    color: muted
                    font.pixelSize: 11
                }
            }
            Column {
                anchors.centerIn: parent
                visible: cadController.parts.length === 0
                spacing: 8
                Label {
                    text: "No assembly loaded"
                    color: window.text
                    font.pixelSize: 25
                }
                Label {
                    text: "Import a real STEP file to inspect its tessellation."
                    color: muted
                }
            }
        }
        Rectangle {
            Layout.minimumWidth: 285
            Layout.maximumWidth: 285
            Layout.fillHeight: true
            color: panel
            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Label {
                    text: "PROPERTIES   •   EVIDENCE"
                    color: muted
                    font.bold: true
                    font.pixelSize: 11
                }
                Rectangle {
                    width: parent.width
                    height: 1
                    color: line
                }
                Label {
                    text: hasSelection ? selectedPart.name : "No selection"
                    color: window.text
                    font.pixelSize: 18
                }
                Rectangle {
                    visible: hasSelection
                    width: parent.width
                    height: 168
                    color: "#151b20"
                    border.color: line
                    radius: 3
                    clip: true
                    View3D {
                        anchors.fill: parent
                        anchors.margins: 1
                        camera: detailCamera
                        environment: SceneEnvironment {
                            clearColor: "#151b20"
                            backgroundMode: SceneEnvironment.Color
                            antialiasingMode: SceneEnvironment.MSAA
                            antialiasingQuality: SceneEnvironment.High
                        }
                        PerspectiveCamera {
                            id: detailCamera
                            z: hasSelection ? Math.max(0.025, Math.max(selectedPart.sizeX, selectedPart.sizeY, selectedPart.sizeZ) * 2.25) : 0.1
                            clipNear: 0.00001
                            clipFar: 10
                        }
                        DirectionalLight {
                            eulerRotation.x: -35
                            eulerRotation.y: -35
                            brightness: 1.5
                        }
                        DirectionalLight {
                            eulerRotation.x: 30
                            eulerRotation.y: 140
                            brightness: 0.5
                            color: "#a9d2ed"
                        }
                        Node {
                            visible: hasSelection
                            eulerRotation.x: -90
                            eulerRotation.y: -28
                            Model {
                                geometry: hasSelection ? selectedPart.geometry : null
                                position: hasSelection ? Qt.vector3d(-selectedPart.centerX, -selectedPart.centerY, -selectedPart.centerZ) : Qt.vector3d(0, 0, 0)
                                materials: [
                                    PrincipledMaterial {
                                        baseColor: "#73b8de"
                                        roughness: 0.42
                                        metalness: 0.08
                                    }
                                ]
                            }
                        }
                    }
                    Label {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 7
                        text: "ISOLATED SELECTION"
                        color: muted
                        font.bold: true
                        font.pixelSize: 9
                    }
                }
                Row {
                    width: parent.width
                    spacing: 8
                    Label {
                        text: hasSelection ? (selectedPart.componentLabel !== "" ? selectedPart.componentLabel : "Geometry model") : "Select an assembly entity."
                        color: !hasSelection || selectedPart.componentLabel === "" ? muted
                               : !selectedPart.componentVerified ? "#e0ac62"
                               : selectedPart.componentSupersededByRevisionId !== "" ? "#e87972"
                               : "#70c99a"
                    }
                    Label {
                        visible: hasSelection && selectedPart.componentLabel !== "" && !selectedPart.componentVerified
                        text: "cached — unverified since last reopen"
                        color: "#e0ac62"
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        visible: hasSelection && selectedPart.componentVerified && selectedPart.componentSupersededByRevisionId !== ""
                        text: "superseded by a newer published revision — rebind to update"
                        color: "#e87972"
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Button {
                        visible: hasSelection && selectedPart.componentLabel !== "" && !selectedPart.componentVerified && !componentBindingController.busy
                        text: "Reverify"
                        onClicked: cadController.reverifyComponentBinding(window.selectedIndex)
                    }
                    BusyIndicator {
                        visible: componentBindingController.busy
                        running: componentBindingController.busy
                        implicitWidth: 18
                        implicitHeight: 18
                    }
                }
                Label {
                    visible: componentBindingController.error !== ""
                    width: parent.width
                    text: componentBindingController.error
                    color: "#e87972"
                    wrapMode: Text.WordWrap
                }
                Label {
                    visible: componentBindingHistory.length > 0
                    text: "COMPONENT BINDING HISTORY"
                    color: muted
                    font.bold: true
                    font.pixelSize: 10
                    topPadding: 8
                }
                ListView {
                    id: componentBindingHistoryList
                    visible: componentBindingHistory.length > 0
                    width: parent.width
                    height: Math.min(contentHeight, 90)
                    clip: true
                    spacing: 3
                    model: componentBindingHistory
                    delegate: Rectangle {
                        required property var modelData
                        width: componentBindingHistoryList.width
                        height: bindingHistoryText.implicitHeight + 12
                        color: modelData.active ? "#182420" : "#181b1e"
                        border.color: modelData.active ? "#365f4b" : "#2c3238"
                        opacity: modelData.active ? 1.0 : 0.65
                        Label {
                            id: bindingHistoryText
                            anchors.fill: parent
                            anchors.margins: 6
                            text: "rev " + modelData.revision + (modelData.active ? "" : " (superseded)") + "\n" + modelData.package_hash
                            color: modelData.active ? "#9fd6bd" : muted
                            elide: Text.ElideMiddle
                            font.pixelSize: 9
                        }
                    }
                }
                Label {
                    visible: jointHistoryForSelection.length > 0
                    text: "JOINT HISTORY"
                    color: muted
                    font.bold: true
                    font.pixelSize: 10
                    topPadding: 8
                }
                ListView {
                    id: jointHistoryList
                    visible: jointHistoryForSelection.length > 0
                    width: parent.width
                    height: Math.min(contentHeight, 90)
                    clip: true
                    spacing: 3
                    model: jointHistoryForSelection
                    delegate: Rectangle {
                        required property var modelData
                        width: jointHistoryList.width
                        height: jointHistoryText.implicitHeight + 12
                        color: modelData.active ? "#182420" : "#181b1e"
                        border.color: modelData.active ? "#365f4b" : "#2c3238"
                        opacity: modelData.active ? 1.0 : 0.65
                        Label {
                            id: jointHistoryText
                            anchors.fill: parent
                            anchors.margins: 6
                            text: "rev " + modelData.revision + (modelData.active ? "" : " (superseded)") + "\naxis " + modelData.axis + "  [" + modelData.minimum_deg + "°, " + modelData.maximum_deg + "°]  with " + modelData.other_entity_id
                            color: modelData.active ? "#9fd6bd" : muted
                            elide: Text.ElideMiddle
                            font.pixelSize: 9
                        }
                    }
                }
                Label {
                    visible: cadController.sourceName !== ""
                    text: "Source\n" + cadController.sourceName
                    color: muted
                    topPadding: 14
                }
                Label {
                    visible: hasSelection
                    text: "IMPORTED B-REP"
                    color: muted
                    font.bold: true
                    font.pixelSize: 10
                    topPadding: 8
                }
                Label {
                    visible: hasSelection
                    text: hasSelection ? "Bounds (m)\n" + selectedPart.sizeX.toFixed(4) + " × " + selectedPart.sizeY.toFixed(4) + " × " + selectedPart.sizeZ.toFixed(4) + "\nVolume  " + selectedPart.volumeM3.toExponential(3) + " m³\nSurface area  " + selectedPart.surfaceAreaM2.toExponential(3) + " m²\nTopology  " + selectedPart.faceCount + " faces  •  " + selectedPart.edgeCount + " edges" : ""
                    color: muted
                }
                Label {
                    visible: hasSelection
                    text: "Material  unknown\nMass  unknown — not inferred from geometry"
                    color: "#e0ac62"
                    font.pixelSize: 11
                    topPadding: 5
                }
                Label {
                    visible: hasSelection
                    text: hasSelection ? "PLACEMENT OFFSET (m)\nX  " + selectedPart.translationX.toFixed(4) + "   Y  " + selectedPart.translationY.toFixed(4) + "   Z  " + selectedPart.translationZ.toFixed(4) + "\nROTATION XYZ (deg)\nX  " + selectedPart.rotationX.toFixed(2) + "   Y  " + selectedPart.rotationY.toFixed(2) + "   Z  " + selectedPart.rotationZ.toFixed(2) : ""
                    color: hasSelection && (selectedPart.translationX !== 0 || selectedPart.translationY !== 0 || selectedPart.translationZ !== 0 || selectedPart.rotationX !== 0 || selectedPart.rotationY !== 0 || selectedPart.rotationZ !== 0) ? "#70c99a" : muted
                    font.pixelSize: 11
                    topPadding: 5
                }
                Column {
                    visible: hasSelection
                    width: parent.width
                    spacing: 5
                    Label {
                        text: (transformMode === "move" ? "MOVE" : "ROTATE") + " • " + transformFrame.toUpperCase() + " AXES"
                        color: muted
                        font.bold: true
                        font.pixelSize: 10
                    }
                    Row {
                        spacing: 5
                        Repeater {
                            model: ["X", "Y", "Z"]
                            Button {
                                required property string modelData
                                text: "−" + modelData
                                width: 39
                                onClicked: nudgeSelection(modelData, -1)
                            }
                        }
                        Repeater {
                            model: ["X", "Y", "Z"]
                            Button {
                                required property string modelData
                                text: "+" + modelData
                                width: 39
                                onClicked: nudgeSelection(modelData, 1)
                            }
                        }
                    }
                    Label {
                        text: transformMode === "move" ? "Step: " + (moveIncrement * 1000).toFixed(0) + " mm" : "Step: " + rotateIncrement.toFixed(0) + "°"
                        color: muted
                        font.pixelSize: 10
                    }
                    Row {
                        spacing: 5
                        Button {
                            text: "Fine"
                            flat: true
                            onClicked: {
                                if (transformMode === "move")
                                    moveIncrement = 0.001;
                                else
                                    rotateIncrement = 1;
                            }
                        }
                        Button {
                            text: "Medium"
                            flat: true
                            onClicked: {
                                if (transformMode === "move")
                                    moveIncrement = 0.005;
                                else
                                    rotateIncrement = 5;
                            }
                        }
                        Button {
                            text: "Coarse"
                            flat: true
                            onClicked: {
                                if (transformMode === "move")
                                    moveIncrement = 0.025;
                                else
                                    rotateIncrement = 15;
                            }
                        }
                    }
                }
                Label {
                    visible: cadController.interferences.length > 0
                    text: cadController.interferences.length + " confirmed static interference" + (cadController.interferences.length === 1 ? "" : "s")
                    color: "#e87972"
                    font.bold: true
                    topPadding: 8
                }
                Label {
                    visible: cadController.connections.length > 0
                    text: cadController.connections.length + " user-confirmed semantic connection" + (cadController.connections.length === 1 ? "" : "s")
                    color: "#70c99a"
                    font.bold: true
                }
                Label {
                    visible: cadController.error !== ""
                    text: "Import failed\n" + cadController.error
                    color: "#e47a72"
                    wrapMode: Text.WordWrap
                    width: parent.width
                }
            }
        }
    }
    footer: Rectangle {
        height: 25
        color: "#1a2025"
        border.color: line
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            Label {
                text: hasSelection ? "Selected: " + selectedPart.name : "Selected: none"
                color: muted
                font.pixelSize: 11
            }
            Label {
                visible: cadController.warnings.length > 0
                text: "⚠ " + cadController.warnings.join(" • ")
                color: "#e0ac62"
                font.pixelSize: 10
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: "Fixture service: " + (serviceController.online ? "connected" : "offline") + "   •   Units: SI   •   OCCT 7.9.3   •   " + (cadController.busy ? "Importing…" : "Ready")
                color: serviceController.online ? "#74c89c" : muted
                font.pixelSize: 11
            }
        }
    }
    FileDialog {
        id: stepDialog
        title: "Import STEP assembly"
        nameFilters: ["STEP assemblies (*.step *.stp)"]
        onAccepted: cadController.importStepAsync(selectedFile.toLocalFile())
    }
    FolderDialog {
        id: folderDialog
        title: "Open mechanical project folder"
        onAccepted: {
            inventoryDialog.open();
            projectIntakeController.scanFolder(selectedFolder);
        }
    }
    FileDialog {
        id: saveDialog
        title: "Save Prometheus project as version 2"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Prometheus project (*.prometheus)"]
        onAccepted: projectController.saveAsVersion2(selectedFile)
        onRejected: executionController.cancelPendingSaveAsAction()
    }
    FileDialog {
        id: openDialog
        title: "Open Prometheus project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Prometheus project (*.prometheus)"]
        onAccepted: projectController.openProject(selectedFile)
    }
    FileDialog {
        id: recoveryDialog
        title: "Recover from the previous validated project index"
        nameFilters: ["Prometheus projects (*.prometheus)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: projectController.recoverProject(selectedFile)
    }
    FolderDialog {
        id: bundleFolderDialog
        title: "Choose parent folder for portable project bundle"
        onAccepted: projectController.exportPortableBundle(selectedFolder)
    }
    property url pendingRestoreBundleFolder
    FolderDialog {
        id: restoreBundleSourceDialog
        title: "Choose portable project bundle"
        onAccepted: {
            pendingRestoreBundleFolder = selectedFolder
            restoreBundleDestinationDialog.open()
        }
    }
    FolderDialog {
        id: restoreBundleDestinationDialog
        title: "Choose restore destination parent folder"
        onAccepted: projectController.restorePortableBundle(
                        pendingRestoreBundleFolder, selectedFolder)
    }
    Shortcut {
        sequence: "F"
        onActivated: fitView()
    }
    Shortcut {
        sequence: "1"
        onActivated: setView(0, 0, true)
    }
    Shortcut {
        sequence: "2"
        onActivated: setView(-89, 0, true)
    }
    Shortcut {
        sequence: "3"
        onActivated: setView(-24, -38, false)
    }
    Shortcut {
        sequence: "H"
        onActivated: if (hasSelection)
            cadController.toggleVisible(selectedIndex)
    }
    Shortcut {
        sequence: "Shift+H"
        onActivated: cadController.showAll()
    }
    Shortcut {
        sequence: "5"
        onActivated: perspectiveMode = !perspectiveMode
    }
    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: cadController.undoPlacement()
    }
    Shortcut {
        sequence: "Ctrl+Y"
        onActivated: cadController.redoPlacement()
    }
    Shortcut {
        sequence: "W"
        onActivated: panY += cameraDistance * 0.03
    }
    Shortcut {
        sequence: "S"
        onActivated: panY -= cameraDistance * 0.03
    }
    Shortcut {
        sequence: "A"
        onActivated: panX -= cameraDistance * 0.03
    }
    Shortcut {
        sequence: "D"
        onActivated: panX += cameraDistance * 0.03
    }
    Shortcut {
        sequence: "Q"
        onActivated: cameraDistance = Math.min(cadController.sceneDiameter * 20, cameraDistance * 1.12)
    }
    Shortcut {
        sequence: "E"
        onActivated: cameraDistance = Math.max(cadController.sceneDiameter * 0.06, cameraDistance * 0.89)
    }
    Shortcut {
        sequence: "T"
        onActivated: transformMode = "move"
    }
    Shortcut {
        sequence: "R"
        onActivated: transformMode = "rotate"
    }
    Shortcut {
        sequence: "Shift+Left"
        onActivated: panX -= cameraDistance * 0.03
    }
    Shortcut {
        sequence: "Shift+Right"
        onActivated: panX += cameraDistance * 0.03
    }
    Shortcut {
        sequence: "Shift+Up"
        onActivated: panY += cameraDistance * 0.03
    }
    Shortcut {
        sequence: "Shift+Down"
        onActivated: panY -= cameraDistance * 0.03
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (viewInput.interactionMode === "axis" || viewInput.interactionMode === "direct") {
                cadController.cancelPlacementPreview();
                viewInput.interactionMode = "";
                viewInput.draggingAxis = "";
                viewInput.groupPreview = false;
                viewInput.dragged = false;
                viewInput.suppressClick = true;
                viewInput.transientValue = "";
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        color: "#11171dcc"
        visible: cadController.busy || cadController.sweepBusy || cadController.geometryBusy
        z: 100
        Column {
            anchors.centerIn: parent
            spacing: 12
            BusyIndicator {
                running: true
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Label {
                text: cadController.sweepBusy ? "Sweeping joint range with Open Cascade…" : cadController.geometryBusy ? "Recomputing exact placement interference…" : "Importing STEP assembly…"
                color: window.text
            }
            Button {
                visible: cadController.busy
                text: "Cancel"
                onClicked: cadController.cancelImport()
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
    Connections {
        target: projectIntakeController
        function onScanFinished(success) {
            if (success && projectIntakeController.primaryStepPath !== "") {
                inventoryDialog.close();
                cadController.importStepAsync(projectIntakeController.primaryStepPath);
            }
        }
    }
    Connections {
        target: cadController
        function onImportFinished(success) {
            if (success) {
                selectOnly(-1);
                setView(-24, -38, false);
                engineeringController.runGeometryChecks(cadController.interferences, [], false, !cadController.collisionDeferred);
                resultsDialog.open();
            }
        }
    }
    Connections {
        target: cadController
        function onSweepFinished() {
            engineeringController.runGeometryChecks(cadController.interferences, cadController.sweepResults, true, !cadController.collisionDeferred);
            resultsDialog.open();
        }
    }
    Connections {
        target: serviceController
        function onChanged() {
            if (demoResearch && serviceController.candidate.id !== undefined) {
                selectedIndex = Math.min(1, cadController.parts.length - 1);
                if (hasSelection)
                    executionController.setPendingCadEntityId(selectedPart.persistentId);
                componentDialog.open();
            }
        }
    }
    Connections {
        target: executionController
        function onChanged() {
            if (executionController.errorCode === "save_as_required" && executionController.pendingSaveAsAction !== "" && !saveDialog.visible)
                saveDialog.open();
        }
    }
    Popup {
        id: componentDialog
        anchors.centerIn: Overlay.overlay
        width: 1040
        height: 780
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
            radius: 3
        }
        ComponentPackagePanel {
            anchors.fill: parent
            anchors.margins: 20
            serviceController: window.serviceApi
            executionController: window.executionApi
            cadController: cadController
            componentBindingController: componentBindingController
            targetPartIndex: window.selectedIndex
            selectedEntityId: window.executionApi.pendingCadEntityId
            selectedEntityName: window.hasSelection && window.selectedPart.persistentId === window.executionApi.pendingCadEntityId ? window.selectedPart.name : "Selected CAD entity"
            panelColor: panel
            lineColor: line
            textColor: window.text
            mutedColor: muted
            onCloseRequested: componentDialog.close()
        }
    }
    Popup {
        id: inventoryDialog
        anchors.centerIn: Overlay.overlay
        width: 1080
        height: 760
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
            radius: 3
        }
        ProjectInventoryPanel {
            anchors.fill: parent
            anchors.margins: 20
            projectIntakeController: window.intakeApi
            projectController: window.projectApi
            cadPartSelected: window.hasSelection
            panelColor: panel
            lineColor: line
            textColor: window.text
            mutedColor: muted
            onLoadRequested: function (path) {
                inventoryDialog.close();
                cadController.importStepAsync(path);
            }
            onBindCandidateRequested: function (candidate) {
                if (window.hasSelection)
                    cadController.bindProvisionalCandidate(window.selectedIndex, candidate);
            }
            onCloseRequested: inventoryDialog.close()
        }
    }
    Popup {
        id: motorWorkflowDialog
        anchors.centerIn: Overlay.overlay
        width: 1180
        height: 760
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
            radius: 3
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 2
                    Label {
                        text: "PACKAGE-DRIVEN EXECUTION"
                        color: muted
                        font.bold: true
                        font.pixelSize: 11
                    }
                    Label {
                        text: "Reviewed motor-arm analysis"
                        color: window.text
                        font.pixelSize: 23
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "×"
                    flat: true
                    onClicked: motorWorkflowDialog.close()
                }
            }
            TabBar {
                id: motorWorkflowTabs
                Layout.fillWidth: true
                TabButton {
                    text: "Run"
                }
                TabButton {
                    text: "Recorded results (" + executionController.runHistory.length + ")"
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: motorWorkflowTabs.currentIndex
                MotorRunPanel {
                    executionController: window.executionApi
                    projectController: window.projectApi
                    engineeringController: window.engineeringApi
                    panelColor: panel
                    lineColor: line
                    textColor: window.text
                    mutedColor: muted
                    onReviewScenarioRequested: motorScenarioDialog.open()
                }
                RunHistoryPanel {
                    executionController: window.executionApi
                    panelColor: panel
                    lineColor: line
                    textColor: window.text
                    mutedColor: muted
                }
            }
        }
    }
    Popup {
        id: structuralWorkflowDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(1320, window.width - 30)
        height: Math.min(800, window.height - 50)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
            radius: 3
        }
        StructuralSetupPanel {
            anchors.fill: parent
            anchors.margins: 16
            structuralController: window.structuralApi
            panelColor: panel
            lineColor: line
            textColor: window.text
            mutedColor: muted
            onCloseRequested: structuralWorkflowDialog.close()
        }
    }
    MotorScenarioDialog {
        id: motorScenarioDialog
        anchors.centerIn: Overlay.overlay
        executionController: window.executionApi
        projectController: window.projectApi
        panelColor: "#222a31"
        lineColor: line
        textColor: window.text
        mutedColor: muted
    }
    Popup {
        id: moveDialog
        anchors.centerIn: Overlay.overlay
        width: 580
        height: 570
        modal: true
        focus: true
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Label {
                text: "TRANSFORM COMPONENT"
                color: muted
                font.bold: true
            }
            Label {
                text: hasSelection ? selectedPart.name : "Precise placement"
                color: window.text
                font.pixelSize: 22
            }
            Label {
                text: "Canonical translation and extrinsic X→Y→Z rotation propagate to rendering, rotated bounds, measurement, OCCT interference, and joint sweeps."
                color: muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: line
            }
            Label {
                text: "TRANSLATION (m)"
                color: muted
                font.bold: true
                font.pixelSize: 10
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 14
                rowSpacing: 8
                Label {
                    text: "X"
                    color: muted
                }
                TextField {
                    id: moveX
                    validator: DoubleValidator {}
                }
                Label {
                    text: "Y"
                    color: muted
                }
                TextField {
                    id: moveY
                    validator: DoubleValidator {}
                }
                Label {
                    text: "Z"
                    color: muted
                }
                TextField {
                    id: moveZ
                    validator: DoubleValidator {}
                }
            }
            Label {
                text: "ROTATION (deg)"
                color: muted
                font.bold: true
                font.pixelSize: 10
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 14
                rowSpacing: 8
                Label {
                    text: "X"
                    color: muted
                }
                TextField {
                    id: rotateX
                    validator: DoubleValidator {}
                }
                Label {
                    text: "Y"
                    color: muted
                }
                TextField {
                    id: rotateY
                    validator: DoubleValidator {}
                }
                Label {
                    text: "Z"
                    color: muted
                }
                TextField {
                    id: rotateZ
                    validator: DoubleValidator {}
                }
            }
            Label {
                text: "Rotation is about the imported part-bounds center. Interactive drag gizmos are the next layer."
                color: "#e0ac62"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Item {
                Layout.fillHeight: true
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "Reset"
                    onClicked: {
                        cadController.resetPartTranslation(selectedIndex);
                        moveDialog.close();
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Cancel"
                    onClicked: moveDialog.close()
                }
                Button {
                    text: "Apply Transform"
                    highlighted: true
                    onClicked: {
                        cadController.setPartPlacement(selectedIndex, Number(moveX.text), Number(moveY.text), Number(moveZ.text), Number(rotateX.text), Number(rotateY.text), Number(rotateZ.text));
                        moveDialog.close();
                    }
                }
            }
        }
    }
    Popup {
        id: measureDialog
        anchors.centerIn: Overlay.overlay
        width: 560
        height: 460
        modal: true
        focus: true
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Label {
                text: "MEASURE"
                color: muted
                font.bold: true
            }
            Label {
                text: "Part-to-part measurement"
                color: window.text
                font.pixelSize: 22
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: line
            }
            Label {
                text: "First part"
                color: muted
            }
            ComboBox {
                id: measureA
                Layout.fillWidth: true
                model: cadController.parts
                textRole: "name"
            }
            Label {
                text: "Second part"
                color: muted
            }
            ComboBox {
                id: measureB
                Layout.fillWidth: true
                model: cadController.parts
                textRole: "name"
                currentIndex: Math.min(1, cadController.parts.length - 1)
            }
            Button {
                text: "Calculate"
                enabled: measureA.currentIndex !== measureB.currentIndex
                onClicked: measurement = cadController.measureBetween(measureA.currentIndex, measureB.currentIndex)
            }
            Rectangle {
                visible: measurement.center_distance_m !== undefined
                Layout.fillWidth: true
                height: 120
                color: "#1b2228"
                border.color: measurement.broad_phase_overlap ? "#a57a35" : "#365f4b"
                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5
                    Label {
                        text: "Center distance  " + Number(measurement.center_distance_m || 0).toFixed(5) + " m"
                        color: window.text
                        font.bold: true
                    }
                    Label {
                        text: "ΔX " + Number(measurement.dx_m || 0).toFixed(5) + "   ΔY " + Number(measurement.dy_m || 0).toFixed(5) + "   ΔZ " + Number(measurement.dz_m || 0).toFixed(5) + " m"
                        color: muted
                    }
                    Label {
                        text: "AABB clearance  " + Number(measurement.aabb_clearance_m || 0).toFixed(5) + " m"
                        color: muted
                    }
                    Label {
                        text: measurement.broad_phase_overlap ? "Bounding boxes overlap — consult confirmed interference results." : "Bounding boxes are separated."
                        color: measurement.broad_phase_overlap ? "#e0ac62" : "#70c99a"
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Close"
                    onClicked: measureDialog.close()
                }
            }
        }
    }
    Popup {
        id: mateDialog
        anchors.centerIn: Overlay.overlay
        width: 620
        height: 620
        modal: true
        focus: true
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 11
            Label {
                text: "SNAP MATE"
                color: muted
                font.bold: true
            }
            Label {
                text: "Align deterministic geometry anchors"
                color: window.text
                font.pixelSize: 22
            }
            Label {
                text: "Translate one part so its selected anchor coincides with the target anchor. Orientation is preserved."
                color: muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: line
            }
            Label {
                text: "Moving part"
                color: muted
            }
            ComboBox {
                id: mateMoving
                Layout.fillWidth: true
                model: cadController.parts
                textRole: "name"
                currentIndex: Math.min(2, cadController.parts.length - 1)
            }
            Label {
                text: "Moving anchor"
                color: muted
            }
            ComboBox {
                id: mateMovingAnchor
                Layout.fillWidth: true
                model: cadController.placementAnchors(mateMoving.currentIndex)
                textRole: "label"
            }
            Label {
                text: "Target part"
                color: muted
            }
            ComboBox {
                id: mateTarget
                Layout.fillWidth: true
                model: cadController.parts
                textRole: "name"
                currentIndex: 0
            }
            Label {
                text: "Target anchor"
                color: muted
            }
            ComboBox {
                id: mateTargetAnchor
                Layout.fillWidth: true
                model: cadController.placementAnchors(mateTarget.currentIndex)
                textRole: "label"
            }
            Label {
                text: "Connection semantics (user-confirmed)"
                color: muted
            }
            ComboBox {
                id: mateConnectionType
                Layout.fillWidth: true
                model: ["Fixed", "Revolute", "Sliding", "Contact"]
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                color: "#302b21"
                border.color: "#725d35"
                Label {
                    anchors.fill: parent
                    anchors.margins: 9
                    text: "These are bounds-derived placement anchors, not confirmed mounting interfaces. Prometheus will not infer a fastener, joint, fit, or load path from this operation."
                    color: "#d7c39d"
                    wrapMode: Text.WordWrap
                }
            }
            Item {
                Layout.fillHeight: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Cancel"
                    onClicked: mateDialog.close()
                }
                Button {
                    text: "Snap Only"
                    enabled: mateMoving.currentIndex !== mateTarget.currentIndex && mateMovingAnchor.currentIndex >= 0 && mateTargetAnchor.currentIndex >= 0
                    onClicked: {
                        const moving = mateMovingAnchor.model[mateMovingAnchor.currentIndex], target = mateTargetAnchor.model[mateTargetAnchor.currentIndex];
                        if (cadController.snapPlacementAnchors(mateMoving.currentIndex, moving.id, mateTarget.currentIndex, target.id))
                            mateDialog.close();
                    }
                }
                Button {
                    text: "Snap + Confirm"
                    highlighted: true
                    enabled: mateMoving.currentIndex !== mateTarget.currentIndex && mateMovingAnchor.currentIndex >= 0 && mateTargetAnchor.currentIndex >= 0
                    onClicked: {
                        const moving = mateMovingAnchor.model[mateMovingAnchor.currentIndex], target = mateTargetAnchor.model[mateTargetAnchor.currentIndex], type = mateConnectionType.currentText.toLowerCase();
                        if (cadController.confirmAnchorConnection(mateMoving.currentIndex, moving.id, mateTarget.currentIndex, target.id, type))
                            mateDialog.close();
                    }
                }
            }
        }
    }
    Popup {
        id: connectionDialog
        anchors.centerIn: Overlay.overlay
        width: 680
        height: 480
        modal: true
        focus: true
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Label {
                text: "CONNECTION SEMANTICS"
                color: muted
                font.bold: true
            }
            Label {
                text: "Classify confirmed solid engagement"
                color: window.text
                font.pixelSize: 22
            }
            Label {
                text: "Geometry establishes overlap; you establish whether the overlap is intended."
                color: muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: line
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: cadController.interferences
                spacing: 8
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 120
                    color: "#1b2228"
                    border.color: modelData.classification === "prohibited" ? "#b85450" : modelData.classification === "intended_engagement" ? "#365f4b" : "#a57a35"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                text: modelData.first_name + "  ↔  " + modelData.second_name
                                color: window.text
                                font.bold: true
                            }
                            Label {
                                text: Number(modelData.volume_m3).toExponential(3) + " m³ confirmed by OCCT Boolean common-volume"
                                color: muted
                            }
                            Label {
                                text: "Current: " + String(modelData.classification).replace(/_/g, " ")
                                color: modelData.classification === "prohibited" ? "#e87972" : modelData.classification === "intended_engagement" ? "#70c99a" : "#e0ac62"
                            }
                        }
                        ColumnLayout {
                            Label {
                                text: "Classification"
                                color: muted
                            }
                            ComboBox {
                                id: classChoice
                                model: ["Unclassified", "Intended engagement", "Prohibited interference"]
                                currentIndex: modelData.classification === "intended_engagement" ? 1 : modelData.classification === "prohibited" ? 2 : 0
                                onActivated: {
                                    const value = currentIndex === 1 ? "intended_engagement" : currentIndex === 2 ? "prohibited" : "unclassified";
                                    cadController.classifyInterference(modelData.first_id, modelData.second_id, value);
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Classification is project-specific and retained in the project manifest."
                    color: muted
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Done"
                    onClicked: connectionDialog.close()
                }
            }
        }
    }
    Popup {
        id: semanticConnectionsDialog
        anchors.centerIn: Overlay.overlay
        width: 760
        height: 520
        modal: true
        focus: true
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 10
            Label {
                text: "SEMANTIC CONNECTIONS"
                color: muted
                font.bold: true
            }
            Label {
                text: "User-confirmed assembly graph edges"
                color: window.text
                font.pixelSize: 22
            }
            Label {
                text: "Provisional geometry-anchor connections are stored separately from collision results and component-package interface evidence."
                color: muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: line
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: cadController.connections
                spacing: 7
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 92
                    color: "#1b2228"
                    border.color: "#365f4b"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 11
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                text: modelData.source_name + "  →  " + modelData.target_name
                                color: window.text
                                font.bold: true
                            }
                            Label {
                                text: modelData.connection_type.toUpperCase() + "  •  " + modelData.source_anchor + " → " + modelData.target_anchor
                                color: "#70c99a"
                            }
                            Label {
                                text: "Basis: user confirmed • bounds-derived anchors • provisional"
                                color: "#d8b36e"
                                font.pixelSize: 10
                            }
                        }
                        Button {
                            text: "Remove"
                            onClicked: cadController.removeConnection(index)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Removing an edge does not move either part."
                    color: muted
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Done"
                    onClicked: semanticConnectionsDialog.close()
                }
            }
        }
    }
    Popup {
        id: jointDialog
        anchors.centerIn: Overlay.overlay
        width: 520
        height: 430
        modal: true
        focus: true
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Label {
                text: "DEFINE REVOLUTE JOINT"
                color: muted
                font.bold: true
            }
            Label {
                text: "Confirm the semantic connection"
                color: window.text
                font.pixelSize: 22
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: line
            }
            Label {
                text: "Driven component"
                color: muted
            }
            ComboBox {
                id: jointSource
                Layout.fillWidth: true
                model: cadController.parts
                textRole: "name"
                currentIndex: Math.min(1, cadController.parts.length - 1)
            }
            Label {
                text: "Rotating component"
                color: muted
            }
            ComboBox {
                id: jointTarget
                Layout.fillWidth: true
                model: cadController.parts
                textRole: "name"
                currentIndex: Math.min(2, cadController.parts.length - 1)
            }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Label {
                        text: "Axis"
                        color: muted
                    }
                    ComboBox {
                        id: jointAxis
                        model: ["X", "Y", "Z"]
                        currentIndex: 2
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Minimum (deg)"
                        color: muted
                    }
                    TextField {
                        id: jointMin
                        text: "0"
                        validator: DoubleValidator {}
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Maximum (deg)"
                        color: muted
                    }
                    TextField {
                        id: jointMax
                        text: "90"
                        validator: DoubleValidator {}
                    }
                }
            }
            Label {
                text: "The source-part center is used as the joint pivot. Axis, pivot, and limits are user-confirmed because neutral STEP does not preserve mates."
                color: "#e0ac62"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Item {
                Layout.fillHeight: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Cancel"
                    onClicked: jointDialog.close()
                }
                Button {
                    text: "Confirm Joint"
                    highlighted: true
                    onClicked: {
                        const p = cadController.parts[jointSource.currentIndex];
                        const t = cadController.parts[jointTarget.currentIndex];
                        engineeringController.defineRevoluteJoint(jointSource.currentIndex, jointTarget.currentIndex, jointAxis.currentText, Number(jointMin.text), Number(jointMax.text), p.centerX, p.centerY, p.centerZ, p.persistentId, t.persistentId);
                        jointDialog.close();
                    }
                }
            }
        }
    }
    Popup {
        id: resultsDialog
        anchors.centerIn: Overlay.overlay
        width: 900
        height: 700
        modal: true
        focus: true
        background: Rectangle {
            color: "#222a31"
            border.color: "#53616c"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Column {
                    Label {
                        text: "MECHANICAL PROJECT SCREEN"
                        color: muted
                        font.bold: true
                    }
                    Label {
                        text: engineeringController.coverage.status || engineeringController.geometryStatus
                        color: window.text
                        font.pixelSize: 23
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    text: (engineeringController.coverage.evaluated || 0) + " evaluated  •  " + (engineeringController.coverage.not_evaluated || 0) + " not evaluated"
                    color: "#e0ac62"
                }
                Button {
                    text: "×"
                    flat: true
                    onClicked: resultsDialog.close()
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: line
            }
            Label {
                text: "METHODS  •  exact static B-Rep intersections  •  reviewed sampled revolute sweep"
                color: muted
                font.pixelSize: 11
            }
            TabBar {
                id: screenTabs
                Layout.fillWidth: true
                TabButton {
                    text: "Evaluated (" + engineeringController.findings.length + ")"
                }
                TabButton {
                    text: "Not evaluated (" + engineeringController.unknowns.length + ")"
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: screenTabs.currentIndex
                ListView {
                    clip: true
                    model: engineeringController.findings
                    spacing: 7
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: modelData.estimated_range !== "" ? 118 : 104
                        color: "#1b2228"
                        border.color: modelData.status === "fail" ? "#b85450" : modelData.status === "caution" ? "#a57a35" : "#365f4b"
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 11
                            spacing: 12
                            Rectangle {
                                width: 8
                                Layout.fillHeight: true
                                color: modelData.status === "fail" ? "#d96862" : modelData.status === "caution" ? "#d1a650" : "#67bf91"
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: modelData.status.toUpperCase()
                                        color: modelData.status === "fail" ? "#e87972" : modelData.status === "caution" ? "#e0b861" : "#70c99a"
                                        font.bold: true
                                    }
                                    Label {
                                        text: modelData.title
                                        color: window.text
                                        font.bold: true
                                        font.pixelSize: 15
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: (modelData.unit === "m³" ? Number(modelData.calculated).toExponential(3) : Number(modelData.calculated).toFixed(3)) + " " + modelData.unit + ((modelData.available || 0) !== 0 ? "  /  limit " + Number(modelData.available).toFixed(3) : "")
                                        color: window.text
                                    }
                                }
                                Label {
                                    text: modelData.mechanism
                                    color: muted
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                                Label {
                                    visible: modelData.estimated_range !== ""
                                    text: "Estimated range: " + modelData.estimated_range
                                    color: "#d8b36e"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: "Evidence: " + modelData.evidence
                                    color: "#83b6d5"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    visible: modelData.assumption !== ""
                                    text: "Boundary: " + modelData.assumption
                                    color: "#d8b36e"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
                ListView {
                    clip: true
                    model: engineeringController.unknowns
                    spacing: 7
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 118
                        color: "#1b2228"
                        border.color: "#a57a35"
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            Rectangle {
                                width: 8
                                Layout.fillHeight: true
                                color: "#d1a650"
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: "NOT EVALUATED"
                                        color: "#e0b861"
                                        font.bold: true
                                    }
                                    Label {
                                        text: modelData.question
                                        color: window.text
                                        font.bold: true
                                        font.pixelSize: 15
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                }
                                Label {
                                    text: modelData.reason
                                    color: muted
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: "To evaluate: " + modelData.unlock
                                    color: "#83b6d5"
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Unknowns and deferred checks never become a project-wide pass."
                    color: muted
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Close"
                    onClicked: resultsDialog.close()
                }
            }
        }
    }
}
