#include "CompositionModel.h"

// Pass a pointer to the member container so QRangeModel operates on it in place;
// structural changes go through the QAbstractItemModel API. As with TimetableModel,
// the base is constructed before m_vehicles — safe because QRangeModel only
// introspects CompositionVehicle's metaobject during construction, never the data.
CompositionModel::CompositionModel(QObject *parent)
    : QRangeModel(&m_vehicles, parent)
{
}

void CompositionModel::setVehicles(const QVector<CompositionVehicle> &vehicles)
{
    // Full replace (a consist is small and arrives wholesale).
    beginResetModel();
    m_vehicles = vehicles;
    endResetModel();
    emit countChanged();
}

void CompositionModel::clear()
{
    if (m_vehicles.isEmpty())
        return;
    beginResetModel();
    m_vehicles.clear();
    endResetModel();
    emit countChanged();
}
