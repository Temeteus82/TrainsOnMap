#pragma once

#include <QDateTime>
#include <QRangeModel>
#include <QString>
#include <QVector>
#include <QtQmlIntegration>

/// One row on a station departure board: a train calling at the station, with the
/// time it does so, where it's headed, its track and live delay.
///
/// Q_GADGET whose Q_PROPERTYs are the model's row schema (QRangeModel reflects each
/// into a QML role of the same name). `sortTime` has no Q_PROPERTY on purpose — it
/// orders the rows but isn't a role (like TimetableStop's build-time accumulators).
struct StationBoardRow {
    Q_GADGET
    Q_PROPERTY(QString trainLabel MEMBER trainLabel)     ///< "IC 55", "R", …
    Q_PROPERTY(QString destination MEMBER destination)   ///< final-stop name
    Q_PROPERTY(QString timeText MEMBER timeText)         ///< scheduled "HH:mm"
    Q_PROPERTY(QString estimateText MEMBER estimateText) ///< live "HH:mm" if it differs, else ""
    Q_PROPERTY(QString track MEMBER track)               ///< commercial track, may be ""
    Q_PROPERTY(int delayMinutes MEMBER delayMinutes)
    Q_PROPERTY(bool cancelled MEMBER cancelled)
    Q_PROPERTY(bool arriving MEMBER arriving)            ///< true = terminates here (arrival), false = departure
public:
    QString trainLabel;
    QString destination;
    QString timeText;
    QString estimateText;
    QString track;
    int delayMinutes = 0;
    bool cancelled = false;
    bool arriving = false;
    QDateTime sortTime;   ///< ordering key; deliberately not a role
};

// Named roles on one item (Qt 6.11+), as the other QRangeModel gadgets do.
template <>
struct QRangeModel::RowOptions<StationBoardRow> {
    static constexpr auto rowCategory = QRangeModel::RowCategory::MultiRoleItem;
};

/// List model behind the station board panel — one row per calling train, ordered
/// by time. Owned by StationBoardService and exposed via its `board` property.
class StationBoardModel : public QRangeModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtain via StationBoardService.board")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit StationBoardModel(QObject *parent = nullptr);

    int count() const { return m_rows.size(); }

    void setRows(const QVector<StationBoardRow> &rows);
    void clear();

signals:
    void countChanged();

private:
    QVector<StationBoardRow> m_rows;
};
