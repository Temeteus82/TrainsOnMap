#include "StationListModel.h"

// Pass a pointer to the member container so QRangeModel operates on it in place;
// the base is constructed before m_stations — safe because QRangeModel only
// introspects StationPoint's metaobject during construction, never the data
// (same pattern as CompositionModel/TimetableModel).
StationListModel::StationListModel(QObject *parent)
    : QRangeModel(&m_stations, parent)
{
    // Drive `count` off the model's own structural signals rather than only off the
    // hand-written setters below: a row change through the inherited QRangeModel
    // write API would otherwise move rowCount() without notifying, leaving QML
    // `count` bindings stale (review CPP-O3). Same wiring as TrainFilterModel.
    connect(this, &QAbstractItemModel::rowsInserted, this, &StationListModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &StationListModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &StationListModel::countChanged);
}

void StationListModel::setStations(const QVector<StationPoint> &stations)
{
    // Full replace — the station set is loaded once, wholesale.
    beginResetModel();
    m_stations = stations;
    endResetModel();
}
