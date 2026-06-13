#pragma once

#include <QObject>
#include <QTimer>
#include <QtQmlIntegration>

#include "TrainListModel.h"

class QNetworkAccessManager;
class QNetworkReply;

/// Polls the Digitraffic "train-locations/latest" REST endpoint and feeds the
/// decoded positions into a TrainListModel.
///
/// API reference: https://www.digitraffic.fi/rautatieliikenne/
///   GET https://rata.digitraffic.fi/api/v1/train-locations/latest/
///   -> [ { trainNumber, location:{type:"Point",coordinates:[lon,lat]},
///          speed, timestamp, departureDate, accuracy }, ... ]
class DigitrafficClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TrainListModel *model READ model CONSTANT)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int pollIntervalMs READ pollIntervalMs WRITE setPollIntervalMs NOTIFY pollIntervalMsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit DigitrafficClient(QObject *parent = nullptr);

    TrainListModel *model() const { return m_model; }

    bool isActive() const { return m_active; }
    void setActive(bool active);

    int pollIntervalMs() const { return m_timer.interval(); }
    void setPollIntervalMs(int ms);

    QString status() const { return m_status; }

public slots:
    /// Fetch the latest positions once, immediately.
    void refresh();

signals:
    void activeChanged();
    void pollIntervalMsChanged();
    void statusChanged();

private:
    void handleReply(QNetworkReply *reply);
    void setStatus(const QString &status);

    QNetworkAccessManager *m_net = nullptr;
    TrainListModel *m_model = nullptr;
    QTimer m_timer;
    bool m_active = false;
    QString m_status;
};
