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
    // Full replace (timetables are small and arrive wholesale). beginResetModel
    // tells attached views to re-read; QRangeModel reports the live size of the
    // backing container afterwards.
    beginResetModel();
    m_stops = stops;
    endResetModel();
    emit countChanged();
}

void TimetableModel::clear()
{
    if (m_stops.isEmpty())
        return;
    beginResetModel();
    m_stops.clear();
    endResetModel();
    emit countChanged();
}
