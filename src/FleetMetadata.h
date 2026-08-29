#pragma once

#include "DigitrafficClient.h"
#include "DigitrafficFormat.h"

#include <QHash>
#include <QString>

/// Read-through view of the three metadata maps DigitrafficClient loads once per
/// launch: station short code -> name, and the two cause-category code -> name
/// maps.
///
/// StationBoardService and TrainDetailsService render the same Digitraffic fields
/// in different panels, and each had grown its own copy of these three maps, of
/// the handlers that refreshed them, and of the resolvers that read them — copies
/// that had already drifted (review CPP-W5 / CPP-O1). Nothing ever writes to the
/// copies: they were only ever assigned wholesale from the client that owns the
/// originals. So there is nothing to cache — hold the pointer and look through it.
///
/// Deliberately not a QObject: each service keeps its own `fleet` property and
/// its own signal wiring (the handlers differ in what they rebuild afterwards),
/// and hands the pointer here as one line of that.
class FleetMetadata
{
public:
    /// `fleet` is not owned and must outlive this — it is the same app-lifetime
    /// client the owning service already holds as its `fleet` property.
    void setFleet(const DigitrafficClient *fleet) { m_fleet = fleet; }

    /// False until the matching one-shot metadata fetch lands; the services use
    /// these to decide whether a late-arriving map is worth a rebuild.
    bool stationsLoaded() const { return !stationNames().isEmpty(); }
    bool causesLoaded() const { return m_fleet && !m_fleet->causeCategoryNames().isEmpty(); }

    /// Display name for a station short code; see digitraffic::stationLabel.
    QString stationLabel(const QString &code, const QString &fallback = {}) const
    {
        return digitraffic::stationLabel(stationNames(), code, fallback);
    }

    /// Human-readable delay reason for a timetable row; see digitraffic::causeText.
    QString causeText(const QString &causeCode, const QString &causeDetailedCode) const
    {
        if (!m_fleet)
            return {};
        return digitraffic::causeText(causeCode, causeDetailedCode,
                                      m_fleet->causeCategoryNames(),
                                      m_fleet->detailedCauseCategoryNames());
    }

private:
    /// The station map, or a shared empty one while no fleet is wired — so the
    /// resolvers above stay branch-free and never copy the hash.
    const QHash<QString, QString> &stationNames() const
    {
        static const QHash<QString, QString> none;
        return m_fleet ? m_fleet->stationNames() : none;
    }

    const DigitrafficClient *m_fleet = nullptr;   ///< not owned
};
