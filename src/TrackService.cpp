#include "TrackService.h"

#include "Projection.h"

#include <QByteArray>
#include <QFile>
#include <QFutureWatcher>
#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrentRun>

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
    // Parse + project the network off the GUI thread so the ~200 ms startup cost
    // doesn't block the first frame. The result is applied back on the GUI thread
    // (QFutureWatcher::finished), then geometryReady() lets QML load the first
    // viewport.
    setLoading(true);
    setStatus(QStringLiteral("Loading rail geometry…"));

    auto *watcher = new QFutureWatcher<QVector<Segment>>(this);
    connect(watcher, &QFutureWatcher<QVector<Segment>>::finished, this, [this, watcher] {
        m_all = watcher->result();
        watcher->deleteLater();
        setLoading(false);
        setStatus(m_all.isEmpty()
                      ? QStringLiteral("Rail geometry could not be loaded")
                      : QStringLiteral("%1 track segments ready").arg(m_all.size()));
        emit geometryReady();
    });
    watcher->setFuture(QtConcurrent::run(&TrackService::parseGeometry));
}

QVector<TrackService::Segment> TrackService::parseGeometry()
{
    QVector<Segment> segments;

    QFile file(QString::fromLatin1(kResourcePath));
    if (!file.open(QIODevice::ReadOnly))
        return segments;
    const QByteArray raw = qUncompress(file.readAll());
    file.close();
    if (raw.isEmpty())
        return segments;

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return segments;
    const QJsonArray features =
        doc.object().value(QStringLiteral("features")).toArray();

    // Project one GeoJSON ring (EPSG:3067 easting/northing) to a WGS84 polyline
    // and record its lat/lon bounding box for viewport filtering.
    const auto addLine = [&segments](const QJsonArray &coords) {
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
            // Parenthesised to defeat the windows.h min/max macros (MSVC/MinGW).
            seg.minLat = (std::min)(seg.minLat, c.latitude());
            seg.maxLat = (std::max)(seg.maxLat, c.latitude());
            seg.minLon = (std::min)(seg.minLon, c.longitude());
            seg.maxLon = (std::max)(seg.maxLon, c.longitude());
        }
        if (seg.path.size() >= 2)
            segments.push_back(std::move(seg));
    };

    segments.reserve(features.size());
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

    return segments;
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
