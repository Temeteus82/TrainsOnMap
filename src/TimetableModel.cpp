#include "TimetableModel.h"

#include <utility>

/// Role-visible equality for one stop — everything a delegate renders. The
/// build-time accumulators are deliberately excluded: they are not roles, and
/// `sawActual` feeds `passed`/`isNext`, which *are* compared.
static bool sameVisible(const TimetableStop &a, const TimetableStop &b)
{
    return a.stationShortCode == b.stationShortCode
        && a.stationName == b.stationName
        && a.scheduledArrival == b.scheduledArrival
        && a.estimatedArrival == b.estimatedArrival
        && a.scheduledDeparture == b.scheduledDeparture
        && a.estimatedDeparture == b.estimatedDeparture
        && a.delayMinutes == b.delayMinutes
        && a.track == b.track
        && a.cancelled == b.cancelled
        && a.stopping == b.stopping
        && a.passed == b.passed
        && a.isNext == b.isNext
        && a.causeText == b.causeText;
}

// Pass a pointer to the member container so QRangeModel operates on it in place;
// structural changes below go through the QAbstractItemModel API. NOTE: the base
// is constructed before m_stops, so QRangeModel must not dereference the pointer
// during construction — it only introspects TimetableStop's metaobject (a
// type-level operation) to build the role table, which is safe.
TimetableModel::TimetableModel(QObject *parent)
    : QRangeModel(&m_stops, parent)
{
    // Drive `count` off the model's own structural signals rather than only off the
    // hand-written setters below: a row change through the inherited QRangeModel
    // write API would otherwise move rowCount() without notifying, leaving QML
    // `count` bindings stale (review CPP-O3). Same wiring as TrainFilterModel.
    connect(this, &QAbstractItemModel::rowsInserted, this, &TimetableModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TimetableModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TimetableModel::countChanged);
}

void TimetableModel::setStops(QVector<TimetableStop> rows)
{
    // Derive journey progress from the per-stop `sawActual` flag: the last point
    // that recorded an actualTime is the furthest the train has demonstrably
    // reached, so every point up to and including it is `passed`, and the first
    // booked stop after it is `isNext`. Recomputed on every (re)build, so live
    // MQTT updates advance the marker as the train moves.
    int lastPassed = -1;
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].sawActual)
            lastPassed = i;
    int next = -1;
    for (int i = lastPassed + 1; i < rows.size(); ++i)
        if (rows[i].stopping) {
            next = i;
            break;
        }

    int total = 0;
    int passed = 0;
    for (int i = 0; i < rows.size(); ++i) {
        rows[i].passed = (i <= lastPassed);
        rows[i].isNext = (i == next);
        if (rows[i].stopping) {
            ++total;
            if (rows[i].passed)
                ++passed;
        }
    }

    // A reset destroys every instantiated delegate and drops the scroll
    // position — and this is called on *every* live MQTT update, where the stop
    // list is structurally identical and only a few estimate/delay/progress
    // fields moved (CPP-W3). So when the station sequence is unchanged, assign
    // in place and emit one dataChanged over the span that actually changed;
    // the reset is kept for a genuinely new selection.
    bool sameStructure = rows.size() == m_stops.size();
    for (int i = 0; sameStructure && i < rows.size(); ++i)
        sameStructure = rows.at(i).stationShortCode == m_stops.at(i).stationShortCode;

    if (sameStructure) {
        int first = -1, last = -1;
        for (int i = 0; i < rows.size(); ++i) {
            if (sameVisible(rows.at(i), m_stops.at(i)))
                continue;
            if (first < 0)
                first = i;
            last = i;
        }
        m_stops = std::move(rows);
        if (first >= 0)
            emit dataChanged(index(first, 0), index(last, 0));
    } else {
        // Full replace. beginResetModel tells attached views to re-read;
        // QRangeModel reports the live size of the backing container afterwards.
        beginResetModel();
        m_stops = std::move(rows);
        endResetModel();
    }

    const bool progressMoved =
        passed != m_passedStops || total != m_totalStops || next != m_nextStopRow;
    m_passedStops = passed;
    m_totalStops = total;
    m_nextStopRow = next;
    if (progressMoved || !sameStructure)
        emit progressChanged();
}

void TimetableModel::clear()
{
    if (m_stops.isEmpty())
        return;
    beginResetModel();
    m_stops.clear();
    endResetModel();
    m_passedStops = 0;
    m_totalStops = 0;
    m_nextStopRow = -1;
    emit progressChanged();
}
