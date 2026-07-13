#include "WeatherStationModel.h"

// Base constructed with a pointer to the member container (in-place), before
// m_points — safe because QRangeModel only introspects the metaobject at
// construction. Same pattern as the other QRangeModel models here.
WeatherStationModel::WeatherStationModel(QObject *parent)
    : QRangeModel(&m_points, parent)
{
}

void WeatherStationModel::setPoints(const QVector<WeatherPoint> &points)
{
    beginResetModel();
    m_points = points;
    endResetModel();
    emit countChanged();
}

void WeatherStationModel::clear()
{
    if (m_points.isEmpty())
        return;
    beginResetModel();
    m_points.clear();
    endResetModel();
    emit countChanged();
}
