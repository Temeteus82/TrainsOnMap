#include "TrackService.h"

#include "Projection.h"

#include <QByteArray>
#include <QFile>
#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {
// Pre-baked, Qt-compressed national rail network embedded as a resource
// (see scripts/bake_rails.py). qUncompress reads the qCompress container.
constexpr auto kResourcePath = ":/data/rails.geojson.qz";
}

TrackService::TrackService(QObject *parent)
    : QObject(parent)
    , m_model(new TrackListModel(this))
{
    loadGeometry();
}

void TrackService::loadGeometry()
{
    QFile file(QString::fromLatin1(kResourcePath));
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("Rail geometry resource missing"));
        return;
    }
    const QByteArray raw = qUncompress(file.readAll());
    file.close();
    if (raw.isEmpty()) {
        setStatus(QStringLiteral("Rail geometry could not be decompressed"));
        return;
    }

    const QJsonArray features =
        QJsonDocument::fromJson(raw).object().value(QStringLiteral("features")).toArray();

    // Project one GeoJSON ring (EPSG:3067 easting/northing) to a WGS84 polyline
    // and record its lat/lon bounding box for viewport filtering.
    const auto addLine = [this](const QJsonArray &coords) {
        Segment seg;
        seg.path.reserve(coords.size());
        seg.minLat = seg.minLon = 1e9;
        seg.maxLat = seg.maxLon = -1e9;
        for (const QJsonValue &cv : coords) {
            const QJsonArray p = cv.toArray();
            if (p.size() < 2)
                continue;
            const QGeoCoordinate c = tm35fin::toWgs84(p.at(0).toDouble(), p.at(1).toDouble());
            if (!c.isValid())
                continue;
            seg.path.append(QVariant::fromValue(c));
            seg.minLat = std::min(seg.minLat, c.latitude());
            seg.maxLat = std::max(seg.maxLat, c.latitude());
            seg.minLon = std::min(seg.minLon, c.longitude());
            seg.maxLon = std::max(seg.maxLon, c.longitude());
        }
        if (seg.path.size() >= 2)
            m_all.push_back(std::move(seg));
    };

    m_all.clear();
    m_all.reserve(features.size());
    for (const QJsonValue &fv : features) {
        const QJsonObject geom = fv.toObject().value(QStringLiteral("geometry")).toObject();
        const QString type = geom.value(QStringLiteral("type")).toString();
        const QJsonArray coords = geom.value(QStringLiteral("coordinates")).toArray();
        if (type == QLatin1String("LineString")) {
            addLine(coords);
        } else if (type == QLatin1String("MultiLineString")) {
            for (const QJsonValue &part : coords)
                addLine(part.toArray());
        }
    }

    setStatus(QStringLiteral("%1 track segments ready").arg(m_all.size()));
}

void TrackService::load()
{
    QVector<QVariantList> all;
    all.reserve(m_all.size());
    for (const Segment &s : m_all)
        all.push_back(s.path);
    m_model->setSegments(all);
    setStatus(QStringLiteral("%1 track segments").arg(all.size()));
}

void TrackService::loadForBounds(double west, double south, double east, double north)
{
    QVector<QVariantList> visible;
    for (const Segment &s : m_all) {
        // Keep segments whose bbox intersects the viewport box.
        if (s.maxLat < south || s.minLat > north || s.maxLon < west || s.minLon > east)
            continue;
        visible.push_back(s.path);
    }
    m_model->setSegments(visible);
    setStatus(QStringLiteral("%1 track segments").arg(visible.size()));
}

void TrackService::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void TrackService::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
