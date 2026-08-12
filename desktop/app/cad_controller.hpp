#pragma once
#include <QObject>
#include <QVariantList>
#include <QtQuick3D/QQuick3DGeometry>
#include <QFutureWatcher>
#include "prometheus/cad/types.hpp"
#include <memory>
#include <vector>
#include <cmath>

class MeshGeometry final : public QQuick3DGeometry {
  Q_OBJECT
public: MeshGeometry(const std::vector<float>& positions,const std::vector<std::uint32_t>& indices,QObject* parent=nullptr);
};

class CadPart final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString name READ name CONSTANT)
  Q_PROPERTY(QString persistentId READ persistentId CONSTANT)
  Q_PROPERTY(QQuick3DGeometry* geometry READ geometry CONSTANT)
  Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
  Q_PROPERTY(QString componentRevisionId READ componentRevisionId NOTIFY bindingChanged)
  Q_PROPERTY(QString componentLabel READ componentLabel NOTIFY bindingChanged)
  Q_PROPERTY(double centerX READ centerX CONSTANT) Q_PROPERTY(double centerY READ centerY CONSTANT) Q_PROPERTY(double centerZ READ centerZ CONSTANT)
  Q_PROPERTY(double sizeX READ sizeX CONSTANT) Q_PROPERTY(double sizeY READ sizeY CONSTANT) Q_PROPERTY(double sizeZ READ sizeZ CONSTANT)
  Q_PROPERTY(double volumeM3 READ volumeM3 CONSTANT)
  Q_PROPERTY(double surfaceAreaM2 READ surfaceAreaM2 CONSTANT) Q_PROPERTY(int faceCount READ faceCount CONSTANT) Q_PROPERTY(int edgeCount READ edgeCount CONSTANT)
  Q_PROPERTY(double translationX READ translationX NOTIFY placementChanged) Q_PROPERTY(double translationY READ translationY NOTIFY placementChanged) Q_PROPERTY(double translationZ READ translationZ NOTIFY placementChanged)
  Q_PROPERTY(double rotationX READ rotationX NOTIFY placementChanged) Q_PROPERTY(double rotationY READ rotationY NOTIFY placementChanged) Q_PROPERTY(double rotationZ READ rotationZ NOTIFY placementChanged)
  Q_PROPERTY(double aabbSizeX READ aabbSizeX NOTIFY placementChanged) Q_PROPERTY(double aabbSizeY READ aabbSizeY NOTIFY placementChanged) Q_PROPERTY(double aabbSizeZ READ aabbSizeZ NOTIFY placementChanged)
public:
  CadPart(QString name,QString id,const prometheus::cad::BoundingBox& bounds,double volume,double area,int faces,int edges,std::unique_ptr<MeshGeometry> geometry,QObject* parent=nullptr);
  QString name() const{return name_;} QString persistentId()const{return id_;} QQuick3DGeometry* geometry()const{return geometry_.get();}
  bool visible()const{return visible_;}void setVisible(bool value){if(visible_==value)return;visible_=value;emit visibleChanged();}
  QString componentRevisionId()const{return revision_id_;}QString componentLabel()const{return component_label_;}void bindComponent(QString revision,QString label){revision_id_=std::move(revision);component_label_=std::move(label);emit bindingChanged();}
  double centerX()const{return (bounds_.min_x+bounds_.max_x)/2;}double centerY()const{return (bounds_.min_y+bounds_.max_y)/2;}double centerZ()const{return (bounds_.min_z+bounds_.max_z)/2;}
  double sizeX()const{return bounds_.max_x-bounds_.min_x;}double sizeY()const{return bounds_.max_y-bounds_.min_y;}double sizeZ()const{return bounds_.max_z-bounds_.min_z;}double volumeM3()const{return volume_m3_;}
  double surfaceAreaM2()const{return surface_area_m2_;}int faceCount()const{return face_count_;}int edgeCount()const{return edge_count_;}
  double translationX()const{return tx_;}double translationY()const{return ty_;}double translationZ()const{return tz_;}double rotationX()const{return rx_;}double rotationY()const{return ry_;}double rotationZ()const{return rz_;}
  void setPlacement(double x,double y,double z,double rx,double ry,double rz){tx_=x;ty_=y;tz_=z;rx_=rx;ry_=ry;rz_=rz;emit placementChanged();}
  double aabbSizeX()const;double aabbSizeY()const;double aabbSizeZ()const;
signals:void visibleChanged();void bindingChanged();void placementChanged();
private: QString name_,id_,revision_id_,component_label_;prometheus::cad::BoundingBox bounds_;double volume_m3_{},surface_area_m2_{},tx_{},ty_{},tz_{},rx_{},ry_{},rz_{};int face_count_{},edge_count_{};std::unique_ptr<MeshGeometry> geometry_;bool visible_{true};
};

class CadController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList parts READ parts NOTIFY partsChanged)
  Q_PROPERTY(QString sourceName READ sourceName NOTIFY partsChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  Q_PROPERTY(QStringList warnings READ warnings NOTIFY partsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(double centerX READ centerX NOTIFY partsChanged)
  Q_PROPERTY(double centerY READ centerY NOTIFY partsChanged)
  Q_PROPERTY(double centerZ READ centerZ NOTIFY partsChanged)
  Q_PROPERTY(double sceneDiameter READ sceneDiameter NOTIFY partsChanged)
  Q_PROPERTY(double sceneMinZ READ sceneMinZ NOTIFY partsChanged)
  Q_PROPERTY(QVariantMap engineeringState READ engineeringState NOTIFY engineeringStateChanged)
  Q_PROPERTY(QVariantList interferences READ interferences NOTIFY partsChanged)
  Q_PROPERTY(QVariantList connections READ connections NOTIFY connectionsChanged)
  Q_PROPERTY(QVariantList sweepResults READ sweepResults NOTIFY sweepFinished)
  Q_PROPERTY(bool sweepBusy READ sweepBusy NOTIFY sweepBusyChanged)
  Q_PROPERTY(bool geometryBusy READ geometryBusy NOTIFY geometryBusyChanged)
  Q_PROPERTY(bool collisionDeferred READ collisionDeferred NOTIFY partsChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged) Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
public:
  explicit CadController(QObject* parent=nullptr); QVariantList parts()const{return parts_;} QString sourceName()const{return source_name_;} QString error()const{return error_;}QStringList warnings()const{return warnings_;} bool busy()const{return busy_;}
  double centerX()const{return center_x_;}double centerY()const{return center_y_;}double centerZ()const{return center_z_;}double sceneDiameter()const{return scene_diameter_;}double sceneMinZ()const{return scene_min_z_;}
  QVariantMap engineeringState()const{return engineering_state_;}
  QVariantList interferences()const{return interferences_;}
  QVariantList connections()const{return connections_;}
  QVariantList sweepResults()const{return sweep_results_;}bool sweepBusy()const{return sweep_busy_;}
  bool geometryBusy()const{return geometry_busy_;}
  bool collisionDeferred()const{return collision_deferred_;}
  bool canUndo()const{return !undo_stack_.isEmpty();}bool canRedo()const{return !redo_stack_.isEmpty();}
  Q_INVOKABLE bool importStep(const QString& path); Q_INVOKABLE void importStepAsync(const QString& path); Q_INVOKABLE void cancelImport();
  Q_INVOKABLE bool saveProject(const QString& path); Q_INVOKABLE void openProject(const QString& path);
  Q_INVOKABLE void toggleVisible(int index);Q_INVOKABLE void isolate(int index);Q_INVOKABLE void showAll();
  Q_INVOKABLE void bindComponent(int index,const QVariantMap& revision);
  Q_INVOKABLE QVariantMap measureBetween(int first,int second)const;
  Q_INVOKABLE void classifyInterference(const QString& first_id,const QString& second_id,const QString& classification);
  Q_INVOKABLE void runJointSweepAsync(int moving_index,int excluded_index,double pivot_x,double pivot_y,double pivot_z,const QString& axis,double minimum_deg,double maximum_deg);
  Q_INVOKABLE void setPartTranslation(int index,double x_m,double y_m,double z_m);Q_INVOKABLE void resetPartTranslation(int index);
  Q_INVOKABLE void setPartPlacement(int index,double x_m,double y_m,double z_m,double rx_deg,double ry_deg,double rz_deg);Q_INVOKABLE void undoPlacement();Q_INVOKABLE void redoPlacement();
  Q_INVOKABLE bool beginPlacementPreview(int index);Q_INVOKABLE void previewPartPlacement(int index,double x_m,double y_m,double z_m,double rx_deg,double ry_deg,double rz_deg);Q_INVOKABLE void commitPlacementPreview();Q_INVOKABLE void cancelPlacementPreview();
  Q_INVOKABLE QVariantMap localAxisDirection(int index,const QString& axis)const;Q_INVOKABLE QVariantMap composeLocalRotation(double rx_deg,double ry_deg,double rz_deg,const QString& axis,double delta_deg)const;
  Q_INVOKABLE QVariantList placementAnchors(int index)const;Q_INVOKABLE bool snapPlacementAnchors(int moving_index,const QString& moving_anchor,int target_index,const QString& target_anchor);
  Q_INVOKABLE bool confirmAnchorConnection(int source_index,const QString& source_anchor,int target_index,const QString& target_anchor,const QString& connection_type);Q_INVOKABLE void removeConnection(int index);
  Q_INVOKABLE void setEngineeringState(const QVariantMap& state){engineering_state_=state;emit engineeringStateChanged();}
signals: void partsChanged(); void connectionsChanged();void errorChanged(); void busyChanged(); void sweepBusyChanged();void geometryBusyChanged();void historyChanged();void sweepFinished();void geometryFinished();void engineeringStateChanged();void importFinished(bool success);
private: QVariantList parts_,interferences_,connections_,sweep_results_,undo_stack_,redo_stack_;QString source_name_,source_path_,error_;QStringList warnings_;QVariantList pending_bindings_,pending_interference_classifications_,pending_placements_,pending_connections_;QVariantMap engineering_state_,active_placement_preview_;bool busy_{},sweep_busy_{},geometry_busy_{},collision_deferred_{};double center_x_{},center_y_{},center_z_{},scene_min_z_{},scene_diameter_{1};QFutureWatcher<prometheus::cad::StepImportResult> watcher_;QFutureWatcher<std::vector<prometheus::cad::SweepInterference>> sweep_watcher_;QFutureWatcher<std::vector<prometheus::cad::StaticInterference>> geometry_watcher_;void clearParts();void applyResult(const prometheus::cad::StepImportResult& result);void setBusy(bool value);void applyBindings();void applyInterferenceClassifications();void applyPlacements();void applyConnections();void refreshInterferencesAsync();std::vector<prometheus::cad::PartPlacement> placements()const;QVariantMap placementState(int index)const;void applyPlacementState(const QVariantMap& state);
};
