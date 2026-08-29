#include "WeatherStationModel.h"

// Base constructed with a pointer to the member container (in-place), before
// m_points — safe because QRangeModel only introspects the metaobject at
// construction. Same pattern as the other QRangeModel models here.
WeatherStationModel::WeatherStationModel(QObject *parent)
    : QRangeModel(&m_points, parent)
{
    // Drive `count` off the model's own structural signals rather than only off the
    // hand-written setters below: a row change through the inherited QRangeModel
    // write API would otherwise move rowCount() without notifying, leaving QML
    // `count` bindings stale (review CPP-O3). Same wiring as TrainFilterModel.
    connect(this, &QAbstractItemModel::rowsInserted, this, &WeatherStationModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &WeatherStationModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &WeatherStationModel::countChanged);
}

void WeatherStationModel::setPoints(const QVector<WeatherPoint> &points)
{
    beginResetModel();
    m_points = points;
    endResetModel();
}

void WeatherStationModel::clear()
{
    if (m_points.isEmpty())
        return;
    beginResetModel();
    m_points.clear();
    endResetModel();
}
