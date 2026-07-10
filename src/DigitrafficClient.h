#pragma once

#include <QDateTime>
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

    /// Station short-code -> display name, parsed from the same one-shot
    /// /metadata/stations reply that feeds the model's coordinates. Shared with
    /// TrainDetailsService (via its `fleet` property) so the endpoint is fetched
    /// once per launch. Empty until the fetch lands (stationNamesChanged fires).
    QHash<QString, QString> stationNames() const { return m_stationNames; }

public slots:
    /// Fetch the latest positions once, immediately.
    void refresh();

    /// Refresh marker metadata (category/type/line), running status and scheduled
    /// routes from /live-trains. Pulls only version-deltas (?version=<max seen>)
    /// most cycles, with a periodic full resync to bound memory and let the route
    /// cache evict departed trains. Called by refresh() (i.e. on the resync timer).
    void refreshCategories();

signals:
    void activeChanged();
    void pollIntervalMsChanged();
    void statusChanged();
    void matcherChanged();
    void stationNamesChanged();

private:
    void handleReply(QNetworkReply *reply);
    /// @param full  true for an authoritative full snapshot (accumulated maps are
    ///              reset first); false for an incremental version-delta merge.
    void handleCategories(QNetworkReply *reply, bool full);
    /// One-shot at startup: load station short-code -> coordinate (for the model's
    /// parked-train station pin) and -> name (shared via stationNames()).
    void fetchStations();
    void handleStations(QNetworkReply *reply);
    void setStatus(const QString &status);

    QNetworkAccessManager *m_net = nullptr;
    TrainListModel *m_model = nullptr;
    TrackService *m_matcher = nullptr;
    QTimer m_timer;
    bool m_active = false;
    QString m_status;
    QHash<QString, QString> m_stationNames;   ///< shortCode -> name (see stationNames())

    // ---- /live-trains incremental polling state ----------------------------
    // Highest Train.version seen since the last full snapshot; the next delta
    // request asks for trains modified after this. Reset to 0 on a full resync.
    qint64 m_liveVersion = 0;
    // When the last full (non-delta) /live-trains snapshot was issued; drives the
    // periodic resync. Invalid until the first pull, so the first one is full.
    QDateTime m_lastFullCategories;
    // Accumulated per-train state, kept in sync across deltas and pushed to the
    // model in full each cycle (the model setters replace wholesale). Keyed by
    // (departureDate, trainNumber). A full resync clears and rebuilds these.
    QHash<TrainKey, QString> m_accTypes;
    QHash<TrainKey, QString> m_accCategories;
    QHash<TrainKey, QString> m_accLines;
    QHash<TrainKey, TrainStatus> m_accStatuses;
    QHash<TrainKey, TrainRoute> m_accRoutes;
};
