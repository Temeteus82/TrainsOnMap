#pragma once

#include <QObject>
#include <QTimer>
#include <QtQmlIntegration>

#include "TrainListModel.h"
#include "TrackService.h"

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
    // Optional rail-network matcher (a TrackService); when set, the model snaps
    // and flags incoming GPS fixes against the track geometry.
    Q_PROPERTY(TrackService *matcher READ matcher WRITE setMatcher NOTIFY matcherChanged)

public:
    explicit DigitrafficClient(QObject *parent = nullptr);

    TrainListModel *model() const { return m_model; }

    bool isActive() const { return m_active; }
    void setActive(bool active);

    int pollIntervalMs() const { return m_timer.interval(); }
    void setPollIntervalMs(int ms);

    QString status() const { return m_status; }

    TrackService *matcher() const { return m_matcher; }
    void setMatcher(TrackService *matcher);

public slots:
    /// Fetch the latest positions once, immediately.
    void refresh();

    /// Refresh the trainNumber -> category map from /live-trains (used to colour
    /// markers). Called by refresh() and on a periodic timer.
    void refreshCategories();

signals:
    void activeChanged();
    void pollIntervalMsChanged();
    void statusChanged();
    void matcherChanged();

private:
    void handleReply(QNetworkReply *reply);
    void handleCategories(QNetworkReply *reply);
    void setStatus(const QString &status);

    QNetworkAccessManager *m_net = nullptr;
    TrainListModel *m_model = nullptr;
    TrackService *m_matcher = nullptr;
    QTimer m_timer;
    bool m_active = false;
    QString m_status;
};
