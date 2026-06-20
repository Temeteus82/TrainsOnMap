#pragma once

#include <QRangeModel>
#include <QString>
#include <QVector>
#include <QtQmlIntegration>

/// One stop on a train's journey, merging the ARRIVAL and DEPARTURE timetable
/// rows for a single station into one entry.
///
/// Declared as a Q_GADGET: QRangeModel (Qt 6.10+) reflects each Q_PROPERTY into
/// a QML role named after the property, so this struct *is* the model's row
/// schema — there is no hand-written data()/roleNames()/role enum. The two
/// build-time accumulators are deliberately left without Q_PROPERTY so they stay
/// invisible to the model and to QML.
struct TimetableStop {
    Q_GADGET
    Q_PROPERTY(QString stationName        MEMBER stationName)
    Q_PROPERTY(QString stationShortCode   MEMBER stationShortCode)
    Q_PROPERTY(QString scheduledArrival   MEMBER scheduledArrival)
    Q_PROPERTY(QString estimatedArrival   MEMBER estimatedArrival)
    Q_PROPERTY(QString scheduledDeparture MEMBER scheduledDeparture)
    Q_PROPERTY(QString estimatedDeparture MEMBER estimatedDeparture)
    Q_PROPERTY(int     delayMinutes       MEMBER delayMinutes)
    Q_PROPERTY(QString track              MEMBER track)
    Q_PROPERTY(bool    cancelled          MEMBER cancelled)
    Q_PROPERTY(bool    stopping           MEMBER stopping)
public:
    QString stationShortCode;
    QString stationName;          ///< resolved from station metadata; falls back to code
    QString scheduledArrival;     ///< local "HH:mm", empty for the origin
    QString estimatedArrival;     ///< local "HH:mm", empty when same as scheduled / unknown
    QString scheduledDeparture;   ///< local "HH:mm", empty for the destination
    QString estimatedDeparture;
    int delayMinutes = 0;         ///< latest known difference (departure preferred)
    QString track;                ///< commercial track, may be empty
    bool cancelled = false;
    bool stopping = true;         ///< true = a commercial/booked stop; false = passed through

    // Build-time accumulators (not exposed as roles); used to resolve `stopping`.
    bool sawCommercial = false;
    bool sawTrainStopping = false;
};

// By default QRangeModel maps a gadget's properties to *columns*. A QML list
// needs them as named *roles* on a single item, so opt the row type into
// multi-role representation (Qt 6.11+). With this, roleNames() is derived from
// the Q_PROPERTY names above — exactly what TrainDetailPanel.qml binds to.
template <>
struct QRangeModel::RowOptions<TimetableStop> {
    static constexpr auto rowCategory = QRangeModel::RowCategory::MultiRoleItem;
};

/// List model of a single train's timetable, consumed by the detail panel.
/// Owned by TrainDetailsService and exposed via its `model` property.
///
/// Backed by QRangeModel over an internal QVector<TimetableStop>: the row schema
/// and every role (rowCount/data/roleNames) come from TimetableStop's
/// Q_PROPERTYs, so this class only adds the type-safe replace/clear API and the
/// `count` convenience property. The base ctor is given a pointer to the member
/// container (the encapsulation pattern from the QRangeModel docs), so structural
/// edits go through the QAbstractItemModel API.
class TimetableModel : public QRangeModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtain via TrainDetailsService.model")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit TimetableModel(QObject *parent = nullptr);

    int count() const { return m_stops.size(); }

    void setStops(const QVector<TimetableStop> &stops);
    void clear();

signals:
    void countChanged();

private:
    QVector<TimetableStop> m_stops;
};
