#include "StationBoardModel.h"

// Base constructed with a pointer to the member container (in-place), before
// m_rows — safe because QRangeModel only introspects the metaobject at
// construction. Same pattern as CompositionModel/TimetableModel.
StationBoardModel::StationBoardModel(QObject *parent)
    : QRangeModel(&m_rows, parent)
{
    // Drive `count` off the model's own structural signals rather than only off the
    // hand-written setters below: a row change through the inherited QRangeModel
    // write API would otherwise move rowCount() without notifying, leaving QML
    // `count` bindings stale (review CPP-O3). Same wiring as TrainFilterModel.
    connect(this, &QAbstractItemModel::rowsInserted, this, &StationBoardModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &StationBoardModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &StationBoardModel::countChanged);
}

void StationBoardModel::setRows(const QVector<StationBoardRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

void StationBoardModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}
