#pragma once

#include <QObject>
#include <QtQmlIntegration>

#include "TrackListModel.h"

class QNetworkAccessManager;
class QNetworkReply;
class QJsonArray;

/// Fetches railway track geometry from the Digitraffic infra-api as GeoJSON and
/// turns each LineString/MultiLineString feature into polyline segments.
///
/// API reference: https://www.digitraffic.fi/rautatieliikenne/
///   Base GeoJSON: https://rata.digitraffic.fi/infra-api/latest/raiteet.geojson
///   The full network is large (10+ MB), so prefer loadForBounds() to fetch only
///   the current map viewport.
class TrackService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TrackListModel *model READ model CONSTANT)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged)

public:
    explicit TrackService(QObject *parent = nullptr);

    TrackListModel *model() const { return m_model; }
    bool isLoading() const { return m_loading; }
    QString status() const { return m_status; }

    QString endpoint() const { return m_endpoint; }
    void setEndpoint(const QString &endpoint);

public slots:
    /// Load the entire railway network (heavy — use sparingly).
    void load();

    /// Load only tracks intersecting the given WGS84 bounding box.
    /// Arguments follow the GeoJSON/OGC convention: west, south, east, north.
    void loadForBounds(double west, double south, double east, double north);

signals:
    void loadingChanged();
    void statusChanged();
    void endpointChanged();

private:
    void fetch(const QUrl &url);
    void handleReply(QNetworkReply *reply);
    void setLoading(bool loading);
    void setStatus(const QString &status);

    QNetworkAccessManager *m_net = nullptr;
    TrackListModel *m_model = nullptr;
    QNetworkReply *m_inflight = nullptr;   ///< current request; aborted if superseded
    QString m_endpoint = QStringLiteral("https://rata.digitraffic.fi/infra-api/latest/raiteet.geojson");
    bool m_loading = false;
    QString m_status;
};
