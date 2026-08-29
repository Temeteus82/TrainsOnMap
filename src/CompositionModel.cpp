#include "CompositionModel.h"

// Pass a pointer to the member container so QRangeModel operates on it in place;
// structural changes go through the QAbstractItemModel API. As with TimetableModel,
// the base is constructed before m_vehicles — safe because QRangeModel only
// introspects CompositionVehicle's metaobject during construction, never the data.
CompositionModel::CompositionModel(QObject *parent)
    : QRangeModel(&m_vehicles, parent)
{
    // Drive `count` off the model's own structural signals rather than only off the
    // hand-written setters below: a row change through the inherited QRangeModel
    // write API would otherwise move rowCount() without notifying, leaving QML
    // `count` bindings stale (review CPP-O3). Same wiring as TrainFilterModel.
    connect(this, &QAbstractItemModel::rowsInserted, this, &CompositionModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &CompositionModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &CompositionModel::countChanged);
}

void CompositionModel::setVehicles(const QVector<CompositionVehicle> &vehicles)
{
    // Full replace (a consist is small and arrives wholesale).
    beginResetModel();
    m_vehicles = vehicles;
    endResetModel();
}

void CompositionModel::clear()
{
    if (m_vehicles.isEmpty())
        return;
    beginResetModel();
    m_vehicles.clear();
    endResetModel();
}
