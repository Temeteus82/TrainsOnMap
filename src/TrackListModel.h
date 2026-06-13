#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration>

/// List model where each row is one railway track segment: a polyline path
/// expressed as a QVariantList of QGeoCoordinate, ready to bind to a QML
/// MapPolyline.path inside a MapItemView.
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
    };

    explicit TrackListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_segments.size(); }

    void setSegments(const QVector<QVariantList> &segments);

signals:
    void countChanged();

private:
    QVector<QVariantList> m_segments;
};
