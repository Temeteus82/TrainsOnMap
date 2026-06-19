#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>

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
        // Skip empty short codes: DigitrafficClient does the same when building
        // the precompute key, so including them here would make the '|'-joined
        // key diverge and the route-overlay lookup silently miss (#10).
        QStringList codes;
        codes.reserve(m_stops.size());
        for (const TimetableStop &s : m_stops)
            if (!s.stationShortCode.isEmpty())
                codes.push_back(s.stationShortCode);
        return codes;
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

private:
    void fetchStations();
    void handleStations(QNetworkReply *reply);
    void handleTrain(QNetworkReply *reply);
    void applyTrainObject(const QJsonObject &train, bool live);  ///< header + stops
    void onStreamTrainMessage(const QByteArray &payload);        ///< live MQTT update
    void rebuildStops();                 ///< re-resolve names + push to model
    void setLoading(bool loading);
    void setStatus(const QString &status);

    QNetworkAccessManager *m_net = nullptr;
    TimetableModel *m_model = nullptr;
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
};
