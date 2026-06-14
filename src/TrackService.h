#pragma once

#include <QObject>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration>

#include "TrackListModel.h"

/// Provides railway track geometry to the map from a pre-baked snapshot embedded
/// in the binary (`:/data/rails.geojson.qz`, produced by `scripts/bake_rails.py`).
///
/// The rail network changes rarely, so it ships in the repo instead of being
/// fetched from the Digitraffic infra-api on every launch. The whole network is
/// parsed + projected to WGS84 once at startup; loadForBounds() then filters the
/// in-memory segments to the current viewport (rendering the entire network at
/// once would be thousands of polylines).
class TrackService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TrackListModel *model READ model CONSTANT)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit TrackService(QObject *parent = nullptr);

    TrackListModel *model() const { return m_model; }
    bool isLoading() const { return m_loading; }
    QString status() const { return m_status; }

public slots:
    /// Show the entire network (heavy — thousands of segments).
    void load();

    /// Show only tracks intersecting the given WGS84 bounding box.
    /// Arguments follow the GeoJSON/OGC convention: west, south, east, north.
    void loadForBounds(double west, double south, double east, double north);

signals:
    void loadingChanged();
    void statusChanged();

private:
    /// One track segment: its WGS84 polyline plus a lat/lon bbox for fast
    /// viewport filtering.
    struct Segment {
        QVariantList path;   ///< QGeoCoordinate list, bind to MapPolyline.path
        double minLat = 0.0;
        double maxLat = 0.0;
        double minLon = 0.0;
        double maxLon = 0.0;
    };

    /// Parse + project the embedded snapshot into m_all (once, at startup).
    void loadGeometry();
    void setLoading(bool loading);
    void setStatus(const QString &status);

    TrackListModel *m_model = nullptr;
    QVector<Segment> m_all;   ///< the whole network, projected to WGS84
    bool m_loading = false;
    QString m_status;
};
