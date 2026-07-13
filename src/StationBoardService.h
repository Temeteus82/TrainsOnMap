#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QtQmlIntegration>

#include "StationBoardModel.h"

class QNetworkAccessManager;
class QNetworkReply;
class DigitrafficClient;

/// Fetches a station's departure/arrival board on demand and drives the station
/// board panel. Clicking a station dot on the map calls show(code, name); the
/// service pulls the trains calling at that station and turns each into a board
/// row (calling time, destination, track, delay).
///
/// Reuses the same train-object shape the rest of the app already parses.
/// Endpoint (https://www.digitraffic.fi/rautatieliikenne/):
///   GET /api/v1/live-trains/station/{code}?arriving_trains=N&departing_trains=N
///       -> [ { trainType, trainNumber, cancelled, timeTableRows:[...] }, ... ]
class StationBoardService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(StationBoardModel *board READ board CONSTANT)
    Q_PROPERTY(QString stationName READ stationName NOTIFY selectionChanged)
    Q_PROPERTY(QString stationCode READ stationCode NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    /// Source of the station code -> name map (DigitrafficClient's one-shot
    /// /metadata/stations fetch), used to resolve each train's destination name.
    Q_PROPERTY(DigitrafficClient *fleet READ fleet WRITE setFleet NOTIFY fleetChanged)

public:
    explicit StationBoardService(QObject *parent = nullptr);

    StationBoardModel *board() const { return m_board; }
    QString stationName() const { return m_stationName; }
    QString stationCode() const { return m_stationCode; }
    bool hasSelection() const { return m_hasSelection; }
    bool isLoading() const { return m_loading; }
    QString status() const { return m_status; }
    DigitrafficClient *fleet() const { return m_fleet; }
    void setFleet(DigitrafficClient *fleet);

public slots:
    /// Show the board for a station. `name` is a display fallback until (and if)
    /// the fleet's name map resolves the code.
    void show(const QString &code, const QString &name);
    /// Hide the board and clear the model.
    void clear();

signals:
    void selectionChanged();
    void loadingChanged();
    void statusChanged();
    void fleetChanged();

private:
    void onStationNames();                       ///< pull the fleet's name map
    void handleReply(QNetworkReply *reply, const QString &code);
    QString stationLabel(const QString &code) const;   ///< resolved name, code fallback
    void setLoading(bool loading);
    void setStatus(const QString &status);

    QNetworkAccessManager *m_net = nullptr;
    StationBoardModel *m_board = nullptr;
    DigitrafficClient *m_fleet = nullptr;
    QHash<QString, QString> m_stationNames;   ///< shortCode -> name (from `fleet`)

    QString m_stationCode;
    QString m_stationName;
    bool m_hasSelection = false;
    bool m_loading = false;
    QString m_status;
};
