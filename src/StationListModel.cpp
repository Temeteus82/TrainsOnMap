#include "StationListModel.h"

// Pass a pointer to the member container so QRangeModel operates on it in place;
// the base is constructed before m_stations — safe because QRangeModel only
// introspects StationPoint's metaobject during construction, never the data
// (same pattern as CompositionModel/TimetableModel).
StationListModel::StationListModel(QObject *parent)
    : QRangeModel(&m_stations, parent)
{
}

void StationListModel::setStations(const QVector<StationPoint> &stations)
{
    // Full replace — the station set is loaded once, wholesale.
    beginResetModel();
    m_stations = stations;
    endResetModel();
    emit countChanged();
}
