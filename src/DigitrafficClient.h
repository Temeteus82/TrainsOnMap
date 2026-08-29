#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QtQmlIntegration>

#include "TrainListModel.h"
#include "TrackService.h"
#include "StationListModel.h"

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
    // Passenger stations for the map's clickable station layer (station board).
    Q_PROPERTY(StationListModel *stations READ stations CONSTANT)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // True while a refresh() round-trip (position + category fetch) is in
    // flight, so the sidebar can show feedback instead of the Refresh button
    // sitting inert for the duration of the network call (Doherty Threshold).
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    // Live punctuality summary (% on time, ≤5 min late) per broad category,
    // aggregated from the /live-trains delay data already polled each cycle — no
    // extra request. Empty until the first categories fetch lands.
    Q_PROPERTY(QString punctuality READ punctuality NOTIFY punctualityChanged)
    // Optional rail-network matcher (a TrackService); when set, the model snaps
    // and flags incoming GPS fixes against the track geometry.
    Q_PROPERTY(TrackService *matcher READ matcher WRITE setMatcher NOTIFY matcherChanged)

public:
    explicit DigitrafficClient(QObject *parent = nullptr);

    TrainListModel *model() const { return m_model; }
    StationListModel *stations() const { return m_stations; }

    bool isActive() const { return m_active; }
    void setActive(bool active);


    QString status() const { return m_status; }

    bool isLoading() const { return m_pendingRequests > 0; }

    QString punctuality() const { return m_punctuality; }

    TrackService *matcher() const { return m_matcher; }
    void setMatcher(TrackService *matcher);

    /// Station short-code -> display name, parsed from the same one-shot
    /// /metadata/stations reply that feeds the model's coordinates. Shared with
    /// TrainDetailsService (via its `fleet` property) so the endpoint is fetched
    /// once per launch. Empty until the fetch lands (stationNamesChanged fires).
    const QHash<QString, QString> &stationNames() const { return m_stationNames; }

    /// Cause category code -> Finnish display name, from the same one-shot
    /// /metadata/cause-category-codes fetch (e.g. "A" -> "Aikataulu ja
    /// liikennöinti"). Shared with TrainDetailsService for the timetable's delay
    /// cause line. Empty until the fetch lands (causeCategoryNamesChanged fires).
    const QHash<QString, QString> &causeCategoryNames() const { return m_causeCategoryNames; }

    /// Detailed cause category code -> Finnish display name, from the one-shot
    /// /metadata/detailed-cause-category-codes fetch (e.g. "S2" -> "Sähköratavika").
    /// The top-level category alone (e.g. "Sähkörata") is too coarse to be useful;
    /// consumers combine this with causeCategoryNames() for the full reason.
    /// Empty until the fetch lands (causeCategoryNamesChanged fires).
    const QHash<QString, QString> &detailedCauseCategoryNames() const { return m_detailedCauseCategoryNames; }

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
    void statusChanged();
    void loadingChanged();
    void punctualityChanged();
    void matcherChanged();
    void stationNamesChanged();
    void causeCategoryNamesChanged();

private:
    void handleReply(QNetworkReply *reply);
    /// @param full  true for an authoritative full snapshot (accumulated maps are
    ///              reset first); false for an incremental version-delta merge.
    void handleCategories(QNetworkReply *reply, bool full);
    /// Load station short-code -> coordinate (for the model's parked-train
    /// station pin) and -> name (shared via stationNames()). Issued at startup
    /// and re-issued by retryMetadata() until it succeeds; a no-op once loaded
    /// or while a request is already in flight.
    void fetchStations();
    void handleStations(QNetworkReply *reply);
    /// Load cause-category code -> name for the timetable's delay-cause line.
    /// Same retry contract as fetchStations().
    void fetchCauseCategories();
    void handleCauseCategories(QNetworkReply *reply);
    /// Load detailed-cause-category code -> name, paired with
    /// fetchCauseCategories() to make the delay-cause line specific rather than
    /// just the coarse top-level category. Same retry contract as fetchStations().
    void fetchDetailedCauseCategories();
    void handleDetailedCauseCategories(QNetworkReply *reply);
    /// Re-issue whichever of the three metadata fetches has not yet succeeded.
    /// Called from refresh(), i.e. on the resync timer. Without it a single
    /// failure — an app launched before Wi-Fi associates, say — permanently
    /// disables station name resolution, the passenger-station layer,
    /// parked-train pinning and every delay-cause line for the whole process
    /// lifetime. Attempts thin out by doubling the resync cycles skipped
    /// between them, capped so recovery still lands within a bounded delay.
    void retryMetadata();
    /// True while any of the three metadata endpoints is still unloaded, i.e.
    /// names and delay causes are degraded. Reported in status().
    bool metadataIncomplete() const;
    void setStatus(const QString &status);
    /// Recompute the punctuality summary from the accumulated per-train status +
    /// category maps (called at the end of handleCategories).
    void recomputePunctuality();
    /// +1 when issuing a request, -1 when its handler runs (always paired, even
    /// on an error/early-return path). Flips loading()/loadingChanged() only on
    /// the 0 <-> >0 transition, so the two in-flight requests refresh() issues
    /// (position + categories) don't fire the signal twice.
    void adjustPending(int delta);

    QNetworkAccessManager *m_net = nullptr;
    TrainListModel *m_model = nullptr;
    StationListModel *m_stations = nullptr;
    TrackService *m_matcher = nullptr;
    QTimer m_timer;
    bool m_active = false;
    int m_pendingRequests = 0;   ///< see adjustPending()/isLoading()
    QString m_status;
    QString m_punctuality;   ///< see punctuality()
    QHash<QString, QString> m_stationNames;   ///< shortCode -> name (see stationNames())
    QHash<QString, QString> m_causeCategoryNames; ///< categoryCode -> name (see causeCategoryNames())
    QHash<QString, QString> m_detailedCauseCategoryNames; ///< detailedCategoryCode -> name (see detailedCauseCategoryNames())

    // ---- one-shot metadata fetch state (see retryMetadata()) ---------------
    /// `loaded` latches on the first reply that parses; `inFlight` stops a
    /// retry from stacking a second request on top of a slow one.
    struct MetadataFetch {
        bool loaded = false;
        bool inFlight = false;
        /// Worth issuing a request for: not already loaded, none outstanding.
        bool needsRequest() const { return !loaded && !inFlight; }
    };
    MetadataFetch m_stationsFetch;
    MetadataFetch m_causeFetch;
    MetadataFetch m_detailedCauseFetch;
    int m_metadataBackoff = 1;   ///< resync cycles per retry; doubles, capped
    int m_metadataSkips = 0;     ///< cycles left to skip before the next retry

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
