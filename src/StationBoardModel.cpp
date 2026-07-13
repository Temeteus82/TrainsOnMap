#include "StationBoardModel.h"

// Base constructed with a pointer to the member container (in-place), before
// m_rows — safe because QRangeModel only introspects the metaobject at
// construction. Same pattern as CompositionModel/TimetableModel.
StationBoardModel::StationBoardModel(QObject *parent)
    : QRangeModel(&m_rows, parent)
{
}

void StationBoardModel::setRows(const QVector<StationBoardRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
}

void StationBoardModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}
