#pragma once

#include <QRangeModel>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtQmlIntegration>

/// One vehicle in a train's physical makeup — a locomotive or a passenger wagon.
/// Built from a journeySection of the Digitraffic compositions API.
///
/// Like TimetableStop, this is a Q_GADGET whose Q_PROPERTYs *are* the model's row
/// schema: QRangeModel (Qt 6.10+) reflects each property into a QML role of the
/// same name, so there is no hand-written data()/roleNames(). Vehicles are ordered
/// by their physical `position` (the API `location`), locomotive(s) first.
struct CompositionVehicle {
    Q_GADGET
    Q_PROPERTY(int         position    MEMBER position)
    Q_PROPERTY(bool        locomotive  MEMBER locomotive)
    Q_PROPERTY(QString     label       MEMBER label)
    Q_PROPERTY(QString     vehicleType MEMBER vehicleType)
    Q_PROPERTY(QString     powerType   MEMBER powerType)
    Q_PROPERTY(QStringList amenities   MEMBER amenities)
public:
    int position = 0;             ///< physical order in the consist (API `location`)
    bool locomotive = false;      ///< true = locomotive, false = passenger wagon
    QString label;                ///< passenger-facing car number (wagon); empty for a loco
    QString vehicleType;          ///< wagonType ("Ed") or locomotiveType ("Sr2")
    QString powerType;            ///< loco only, e.g. "Electric" / "Diesel"
    QStringList amenities;        ///< human-readable: e.g. ["Catering", "Accessible"]
};

// QRangeModel maps a gadget's properties to columns by default; a QML list needs
// them as named roles on one item, so opt the row type into the multi-role
// representation (Qt 6.11+) — exactly as TimetableModel does for TimetableStop.
template <>
struct QRangeModel::RowOptions<CompositionVehicle> {
    static constexpr auto rowCategory = QRangeModel::RowCategory::MultiRoleItem;
};

/// List model of a single train's carriage order, consumed by the detail panel's
/// carriage strip. Owned by TrainDetailsService and exposed via its `composition`
/// property. Backed by QRangeModel over an internal QVector<CompositionVehicle>:
/// the row schema and every role come from CompositionVehicle's Q_PROPERTYs, so
/// this class only adds the type-safe replace/clear API and a `count` convenience.
class CompositionModel : public QRangeModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtain via TrainDetailsService.composition")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit CompositionModel(QObject *parent = nullptr);

    int count() const { return m_vehicles.size(); }

    void setVehicles(const QVector<CompositionVehicle> &vehicles);
    void clear();

signals:
    void countChanged();

private:
    QVector<CompositionVehicle> m_vehicles;
};
