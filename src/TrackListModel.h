#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration>

/// List model where each row is one railway track segment: a polyline path
/// expressed as a QVariantList of QGeoCoordinate, ready to bind to a QML
/// MapPolyline.path inside a MapItemView.
///
/// Rows carry the segment's stable id (its index in the full network) so that
/// setVisibleSegments() can diff one viewport against the next and emit only
/// the incremental insert/remove — the MapItemView then rebuilds just the
/// polylines that entered or left the view, not the whole layer on every pan.
///
/// Owned by TrackService and exposed via its `model` property.
class TrackListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtain via TrackService.model")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,  ///< QVariantList<QGeoCoordinate>
        MainTrackRole,                ///< bool: running line (true) vs siding (false)
    };

    explicit TrackListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_paths.size(); }

    /// Replace the visible set. `ids` and `paths` are parallel and must both be
    /// sorted ascending by id (TrackService emits them in network order, which
    /// is ascending). Diffs against the current rows and emits incremental
    /// insert/remove for only the segments that changed between viewports.
    void setVisibleSegments(const QVector<int> &ids, const QVector<QVariantList> &paths,
                            const QVector<bool> &mains);

signals:
    void countChanged();

private:
    QVector<int> m_ids;             ///< segment id (index in the full network), ascending
    QVector<QVariantList> m_paths;  ///< polyline path per row, parallel to m_ids
    QVector<bool> m_main;           ///< main-track flag per row, parallel to m_ids
};
