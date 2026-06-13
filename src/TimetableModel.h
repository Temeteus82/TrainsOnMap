#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QtQmlIntegration>

/// One stop on a train's journey, merging the ARRIVAL and DEPARTURE timetable
/// rows for a single station into one entry.
struct TimetableStop {
    QString stationShortCode;
    QString stationName;          ///< resolved from station metadata; falls back to code
    QString scheduledArrival;     ///< local "HH:mm", empty for the origin
    QString estimatedArrival;     ///< local "HH:mm", empty when same as scheduled / unknown
    QString scheduledDeparture;   ///< local "HH:mm", empty for the destination
    QString estimatedDeparture;
    int delayMinutes = 0;         ///< latest known difference (departure preferred)
    QString track;                ///< commercial track, may be empty
    bool cancelled = false;
};

/// List model of a single train's timetable, consumed by the detail panel.
/// Owned by TrainDetailsService and exposed via its `model` property.
class TimetableModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtain via TrainDetailsService.model")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        StationNameRole = Qt::UserRole + 1,
        StationShortCodeRole,
        ScheduledArrivalRole,
        EstimatedArrivalRole,
        ScheduledDepartureRole,
        EstimatedDepartureRole,
        DelayMinutesRole,
        TrackRole,
        CancelledRole,
    };

    explicit TimetableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_stops.size(); }

    void setStops(const QVector<TimetableStop> &stops);
    void clear();

signals:
    void countChanged();

private:
    QVector<TimetableStop> m_stops;
};
