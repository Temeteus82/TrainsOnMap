#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>

#include "CompositionModel.h"
#include "RailGraph.h"
#include "TimetableModel.h"

class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;
class DigitrafficMqttClient;

/// Fetches a single train's timetable on demand and resolves station codes to
/// names. Drives the timetable detail panel.
///
/// Endpoints (https://www.digitraffic.fi/rautatieliikenne/):
///   GET /api/v1/trains/{departureDate}/{trainNumber}  -> [ { ...timeTableRows } ]
///   GET /api/v1/metadata/stations                     -> [ { stationShortCode, stationName } ]
class TrainDetailsService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TimetableModel *model READ model CONSTANT)
    Q_PROPERTY(CompositionModel *composition READ composition CONSTANT)
    Q_PROPERTY(bool hasComposition READ hasComposition NOTIFY compositionChanged)
    Q_PROPERTY(QString compositionSummary READ compositionSummary NOTIFY compositionChanged) ///< "6 cars · 178 m · max 200 km/h"
    Q_PROPERTY(QString compositionLeg READ compositionLeg NOTIFY compositionChanged)         ///< "Helsinki → Joensuu"
    Q_PROPERTY(int compositionSectionCount READ compositionSectionCount NOTIFY compositionChanged) ///< >1 when the consist changes en route
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString title READ title NOTIFY selectionChanged)        ///< e.g. "IC 55027"
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY selectionChanged)  ///< e.g. "Long-distance · VR"
    Q_PROPERTY(int trainNumber READ trainNumber NOTIFY selectionChanged)
    Q_PROPERTY(QString departureDate READ departureDate NOTIFY selectionChanged)
    Q_PROPERTY(bool cancelled READ cancelled NOTIFY selectionChanged)
    /// Ordered station short codes of the selected train (matches the route key
    /// used by TrackService.routePolyline for the debug overlay).
    Q_PROPERTY(QStringList routeStations READ routeStations NOTIFY routeStationsChanged)
    Q_PROPERTY(DigitrafficMqttClient *stream READ stream WRITE setStream NOTIFY streamChanged)

public:
    explicit TrainDetailsService(QObject *parent = nullptr);

    TimetableModel *model() const { return m_model; }
    CompositionModel *composition() const { return m_composition; }
    bool hasComposition() const { return m_hasComposition; }
    QString compositionSummary() const { return m_compositionSummary; }
    QString compositionLeg() const { return m_compositionLeg; }
    int compositionSectionCount() const { return m_compositionSectionCount; }
    DigitrafficMqttClient *stream() const { return m_stream; }
    void setStream(DigitrafficMqttClient *stream);
    bool hasSelection() const { return m_hasSelection; }
    bool isLoading() const { return m_loading; }
    QString status() const { return m_status; }
    QString title() const { return m_title; }
    QString subtitle() const { return m_subtitle; }
    int trainNumber() const { return m_trainNumber; }
    QString departureDate() const { return m_departureDate; }
    bool cancelled() const { return m_cancelled; }
    QStringList routeStations() const
    {
        // Build the overlay key through the same canonicaliser DigitrafficClient
        // uses for the precompute key — skip empties AND collapse consecutive
        // duplicates — so the two '|'-joined keys can't diverge and silently miss
        // (R8, the robust form of #10: an empty code adjacent to a repeated one
        // previously left a duplicate here that the precompute key didn't have).
        QStringList raw;
        raw.reserve(m_stops.size());
        for (const TimetableStop &s : m_stops)
            raw.push_back(s.stationShortCode);
        return RailGraph::canonicalRouteCodes(raw);
    }

public slots:
    /// Show the timetable for a given run. departureDate is "YYYY-MM-DD".
    void show(int trainNumber, const QString &departureDate);
    /// Hide the detail panel and clear the model.
    void clear();

signals:
    void selectionChanged();
    void loadingChanged();
    void statusChanged();
    void streamChanged();
    void routeStationsChanged();
    void compositionChanged();

private:
    void fetchStations();
    void handleStations(QNetworkReply *reply);
    void handleTrain(QNetworkReply *reply);
    void applyTrainObject(const QJsonObject &train, bool live);  ///< header + stops
    void fetchComposition(int trainNumber, const QString &departureDate);
    void handleComposition(QNetworkReply *reply);
    void clearComposition();             ///< reset carriage state + notify
    void onStreamTrainMessage(const QByteArray &payload);        ///< live MQTT update
    void rebuildStops();                 ///< re-resolve names + push to model
    void setLoading(bool loading);
    void setStatus(const QString &status);
    QString stationLabel(const QString &shortCode) const;   ///< resolved name, code fallback

    QNetworkAccessManager *m_net = nullptr;
    TimetableModel *m_model = nullptr;
    CompositionModel *m_composition = nullptr;
    DigitrafficMqttClient *m_stream = nullptr;

    QHash<QString, QString> m_stationNames;   ///< shortCode -> name
    QVector<TimetableStop> m_stops;           ///< last fetched stops (codes resolved lazily)

    bool m_hasSelection = false;
    bool m_loading = false;
    QString m_status;
    QString m_title;
    QString m_subtitle;
    int m_trainNumber = 0;
    QString m_departureDate;
    bool m_cancelled = false;

    // ---- Carriage composition (fetched once per selection) ------------------
    bool m_hasComposition = false;
    QString m_compositionSummary;   ///< "6 cars · 178 m · max 200 km/h"
    QString m_compositionLeg;       ///< departure-section leg, resolved to names
    int m_compositionSectionCount = 0;
};
