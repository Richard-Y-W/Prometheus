#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QVariantList>

class ServiceController final:public QObject{
  Q_OBJECT
  Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString jobId READ jobId NOTIFY changed)
  Q_PROPERTY(QVariantMap candidate READ candidate NOTIFY changed)
  Q_PROPERTY(QVariantList parameters READ parameters NOTIFY changed)
  Q_PROPERTY(QVariantList events READ events NOTIFY changed)
public:
  explicit ServiceController(QObject* parent=nullptr);bool online()const{return online_;}bool busy()const{return busy_;}QString status()const{return status_;}QString error()const{return error_;}QString jobId()const{return job_id_;}QVariantMap candidate()const{return candidate_;}QVariantList parameters()const{return parameters_;}QVariantList events()const{return events_;}
  Q_INVOKABLE void checkHealth();Q_INVOKABLE void research(const QString& manufacturer,const QString& part_number);Q_INVOKABLE void acceptAndPublish(const QString& reviewer="local-engineer");Q_INVOKABLE void reset();
signals:void onlineChanged();void busyChanged();void changed();void published(QVariantMap revision);
private:
  QNetworkAccessManager network_;bool online_{},busy_{};QString status_,error_,job_id_;QVariantMap candidate_;QVariantList parameters_,events_;void setBusy(bool value);QNetworkRequest request(const QString& path)const;void consumeJob(const QByteArray& data);void fail(const QString& message);
};
