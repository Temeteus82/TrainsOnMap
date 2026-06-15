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

// Map-matching thresholds. A fix within kSnapAcceptMeters of a rail is treated
// as on-track and snapped to it; kSearchMeters bounds the candidate cull so we
// don't scan the whole network per fix. Digitraffic already drops fixes >500 m
// from track, so anything beyond the search radius here is noise or a gap in our
// baked geometry, not a position worth correcting.
constexpr double kSnapAcceptMeters = 150.0;
constexpr double kSearchMeters = 600.0;
constexpr double kMetresPerDegLat = 111320.0;   // ~constant; lon scaled by cos(lat)
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

TrackMatch TrackService::matchToNetwork(const QGeoCoordinate &fix) const
{
    TrackMatch match;   // snapped invalid, distance -1, onTrack false
    if (!fix.isValid() || m_all.isEmpty())
        return match;

    // Work in a local east/north tangent plane (metres) centred on the fix, so
    // point-to-segment distance is a plain Euclidean calc. Accurate at the few-
    // hundred-metre scale we care about; the fix itself maps to the origin.
    const double cosLat = std::cos(fix.latitude() * tm35fin::kPi / 180.0);
    const double mPerLon = kMetresPerDegLat * (std::max)(0.05, cosLat);
    const double latMargin = kSearchMeters / kMetresPerDegLat;
    const double lonMargin = kSearchMeters / mPerLon;

    double best = kSearchMeters;   // ignore anything farther than the search radius
    double bestX = 0.0, bestY = 0.0;
    bool found = false;

    for (const Segment &s : m_all) {
        // Cheap bbox reject: skip segments whose box (grown by the search radius)
        // can't contain the fix.
        if (fix.latitude()  < s.minLat - latMargin || fix.latitude()  > s.maxLat + latMargin
            || fix.longitude() < s.minLon - lonMargin || fix.longitude() > s.maxLon + lonMargin)
            continue;

        // Local-plane coordinates of the previous vertex, carried across the loop.
        bool havePrev = false;
        double ax = 0.0, ay = 0.0;
        for (const QVariant &pv : s.path) {
            const QGeoCoordinate c = pv.value<QGeoCoordinate>();
            const double bx = (c.longitude() - fix.longitude()) * mPerLon;
            const double by = (c.latitude() - fix.latitude()) * kMetresPerDegLat;
            if (havePrev) {
                // Nearest point on segment A->B to the origin, clamped to [0,1].
                const double dx = bx - ax, dy = by - ay;
                const double len2 = dx * dx + dy * dy;
                double t = len2 > 0.0 ? -(ax * dx + ay * dy) / len2 : 0.0;
                t = (std::clamp)(t, 0.0, 1.0);
                const double cx = ax + t * dx, cy = ay + t * dy;
                const double dist = std::sqrt(cx * cx + cy * cy);
                if (dist < best) {
                    best = dist;
                    bestX = cx;
                    bestY = cy;
                    found = true;
                }
            }
            ax = bx;
            ay = by;
            havePrev = true;
        }
    }

    if (!found)
        return match;

    match.snapped = QGeoCoordinate(fix.latitude() + bestY / kMetresPerDegLat,
                                   fix.longitude() + bestX / mPerLon);
    match.distanceMeters = best;
    match.onTrack = best <= kSnapAcceptMeters;
    return match;
}

void TrackService::loadForBounds(double west, double south, double east, double north)
{
    // Segment ids are indices into m_all, so iterating in order yields them
    // ascending — exactly the ordering TrackListModel's incremental diff expects.
    QVector<int> ids;
    QVector<QVariantList> paths;
    for (int i = 0; i < m_all.size(); ++i) {
        const Segment &s = m_all.at(i);
        // Keep segments whose bbox intersects the viewport box.
        if (s.maxLat < south || s.minLat > north || s.maxLon < west || s.minLon > east)
            continue;
        ids.push_back(i);
        paths.push_back(s.path);
    }
    m_model->setVisibleSegments(ids, paths);
    setStatus(QStringLiteral("%1 track segments").arg(ids.size()));
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
