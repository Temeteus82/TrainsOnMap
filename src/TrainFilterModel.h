#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <QtQmlIntegration>

/// View-side proxy over TrainListModel backing the sidebar's train list: filters
/// the live fleet by the sidebar's category toggles and by a free-text search,
/// and sorts by train number so the list is stable to read while rows come and
/// go underneath it.
///
/// The list exists because the map markers were pointer-only (UI audit U2-C1):
/// selecting a train is the app's primary action and had no keyboard or
/// assistive path, and there was no way to find a specific train except scanning
/// the map (U2-O1). Markers could have been made individually focusable instead,
/// but the fleet turns over every few seconds, so the tab order would reshuffle
/// under the user's fingers — a list is both the accessible answer and the
/// findable one.
///
/// Filtering here rather than with `visible: false` delegates is what keeps the
/// ListView virtualising over a fleet of several hundred: an invisible delegate
/// is still instantiated and still contributes no height, so the view keeps
/// building rows until the model is exhausted.
class TrainFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    /// Free-text query. Matched case-insensitively against the train number, the
    /// type ("IC") and the commuter line letter ("R") — i.e. the same fields the
    /// map badge shows, so what the user reads off the map is what they can type.
    /// Empty (default) matches everything.
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    /// The sidebar's three category toggles, so the list shows exactly the trains
    /// the map is showing.
    Q_PROPERTY(bool showCommuter READ showCommuter WRITE setShowCommuter NOTIFY filtersChanged)
    Q_PROPERTY(bool showLongDistance READ showLongDistance WRITE setShowLongDistance NOTIFY filtersChanged)
    Q_PROPERTY(bool showCargo READ showCargo WRITE setShowCargo NOTIFY filtersChanged)
    /// Row count after filtering — the list header reads "N of M".
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit TrainFilterModel(QObject *parent = nullptr);

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);

    bool showCommuter() const { return m_showCommuter; }
    void setShowCommuter(bool show);
    bool showLongDistance() const { return m_showLongDistance; }
    void setShowLongDistance(bool show);
    bool showCargo() const { return m_showCargo; }
    void setShowCargo(bool show);

    int count() const { return rowCount(); }

    void setSourceModel(QAbstractItemModel *sourceModel) override;

signals:
    void searchTextChanged();
    void filtersChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    /// Re-run row acceptance without a full model reset (which would drop the
    /// ListView's delegates and scroll position).
    void refilter();

    QString m_searchText;
    bool m_showCommuter = true;
    bool m_showLongDistance = true;
    bool m_showCargo = true;

    // Cached source role numbers, resolved once in setSourceModel.
    int m_numberRole = -1;
    int m_typeRole = -1;
    int m_lineRole = -1;
    int m_categoryRole = -1;
};
