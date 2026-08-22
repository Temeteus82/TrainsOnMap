#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>

/// Presentation helpers for Digitraffic timetable fields, shared by the two
/// services that render the same API data in different panels
/// (`StationBoardService` and `TrainDetailsService`). Both had grown private
/// copies of these, and the copies drifted — keep them here so a fix lands in
/// both panels at once.
namespace digitraffic {

/// Digitraffic's terms ask every client to identify itself on each request, via
/// the `Digitraffic-User` header (REST) and on the WebSocket handshake (MQTT).
/// All four callers had grown their own copy of this literal, which is how you
/// end up identifying as several different apps; keep the one string here.
///
/// Keep the version in step with `project(... VERSION)` in CMakeLists.txt.
constexpr auto kUserAgent = "TrainsOnMap/0.1 (+https://github.com/Temeteus82/TrainsOnMap)";

/// Parse a Digitraffic ISO8601 timestamp. The API emits fractional seconds on
/// most fields ("2026-07-26T09:12:00.000Z") but not all; one call covers both,
/// because when *parsing*, Qt::ISODateWithMs treats the fractional part as
/// optional and is identical to Qt::ISODate — the two only differ in
/// toString(). (Both services previously carried their own copy of this, one
/// with a second fromString(…, Qt::ISODate) fallback that could never fire.
/// tst_digitrafficformat pins the both-forms behaviour.) Returns an invalid
/// QDateTime if the string doesn't parse at all.
inline QDateTime parseIso(const QString &iso)
{
    return QDateTime::fromString(iso, Qt::ISODateWithMs);
}

/// A Digitraffic timestamp as local "HH:mm". Empty in, empty out — a missing
/// time is normal (e.g. no estimate yet), not an error.
inline QString hhmm(const QString &iso)
{
    if (iso.isEmpty())
        return {};
    const QDateTime dt = parseIso(iso);
    return dt.isValid() ? dt.toLocalTime().toString(QStringLiteral("HH:mm")) : QString();
}

/// Compose the human-readable delay reason from the two cause codes a
/// timeTableRow carries, resolved through the `/metadata/cause-category-codes`
/// maps: "Category: detail", or just the category when the detailed code is
/// unknown. Empty when the row has no cause, or while the metadata is still
/// loading (the services rebuild once it lands).
inline QString causeText(const QString &causeCode,
                         const QString &causeDetailedCode,
                         const QHash<QString, QString> &categoryNames,
                         const QHash<QString, QString> &detailedCategoryNames)
{
    if (causeCode.isEmpty())
        return {};
    const QString category = categoryNames.value(causeCode);
    const QString detail = detailedCategoryNames.value(causeDetailedCode);
    return detail.isEmpty() ? category : QStringLiteral("%1: %2").arg(category, detail);
}

}   // namespace digitraffic
