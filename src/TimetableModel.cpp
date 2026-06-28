#include "TimetableModel.h"

// Pass a pointer to the member container so QRangeModel operates on it in place;
// structural changes below go through the QAbstractItemModel API. NOTE: the base
// is constructed before m_stops, so QRangeModel must not dereference the pointer
// during construction — it only introspects TimetableStop's metaobject (a
// type-level operation) to build the role table, which is safe.
TimetableModel::TimetableModel(QObject *parent)
    : QRangeModel(&m_stops, parent)
{
}

void TimetableModel::setStops(const QVector<TimetableStop> &stops)
{
    QVector<TimetableStop> rows = stops;

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

    // Full replace (timetables are small and arrive wholesale). beginResetModel
    // tells attached views to re-read; QRangeModel reports the live size of the
    // backing container afterwards.
    beginResetModel();
    m_stops = rows;
    endResetModel();

    m_passedStops = passed;
    m_totalStops = total;
    m_nextStopRow = next;
    emit countChanged();
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
    emit countChanged();
    emit progressChanged();
}
