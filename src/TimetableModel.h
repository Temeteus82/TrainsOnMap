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
    Q_PROPERTY(bool    passed             MEMBER passed)
    Q_PROPERTY(bool    isNext             MEMBER isNext)
    Q_PROPERTY(QString causeText          MEMBER causeText)
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
    bool passed = false;          ///< the train has already departed/arrived here (journey progress)
    bool isNext = false;          ///< first booked stop the train hasn't reached yet
    QString causeText;             ///< delay cause category name (e.g. "Onnettomuus"), "" if none

    // Build-time accumulators (not exposed as roles); the raw cause category codes
    // (e.g. "A" / "A1"), resolved to causeText once the code->name maps are available.
    QString causeCode;
    QString causeDetailedCode;
    bool sawCommercial = false;
    bool sawTrainStopping = false;
    bool sawActual = false;       ///< a row recorded an actualTime → the train has been here
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
    // Journey progress over the booked (stopping) stops, derived in setStops().
    Q_PROPERTY(int passedStops READ passedStops NOTIFY progressChanged) ///< booked stops the train has left
    Q_PROPERTY(int totalStops READ totalStops NOTIFY progressChanged)   ///< booked stops on the route
    Q_PROPERTY(int nextStopRow READ nextStopRow NOTIFY progressChanged) ///< source row of the NEXT booked stop; -1 if none/complete

public:
    explicit TimetableModel(QObject *parent = nullptr);

    int count() const { return m_stops.size(); }
    int passedStops() const { return m_passedStops; }
    int totalStops() const { return m_totalStops; }
    int nextStopRow() const { return m_nextStopRow; }

    /// Replaces the rows and derives journey progress. Takes its argument by
    /// value: this is rebuilt on every live MQTT update for the selected train,
    /// so callers holding a temporary should `std::move()` into it.
    void setStops(QVector<TimetableStop> stops);
    void clear();

signals:
    void countChanged();
    void progressChanged();

private:
    QVector<TimetableStop> m_stops;
    int m_passedStops = 0;
    int m_totalStops = 0;
    int m_nextStopRow = -1;
};
