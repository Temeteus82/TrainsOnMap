# TrainsOnMap — C++ Source Reference

Reference documentation for the C++ sources under `src/`. TrainsOnMap is a Qt 6
desktop application that plots live Finnish train positions (Digitraffic feed) and
the national rail-track geometry on a Qt Quick map. The UI is pure QML; the C++
layer below provides the data services, models, and routing/geometry core that QML
binds to.

## Entry point

| File | Description |
|------|-------------|
| [main](main.md) | Startup sequence: creates `QGuiApplication`, sets identity/icon, loads the `TrainsOnMap` QML module, runs the event loop. |

## Networking / data ingest

| Class / File | Description |
|--------------|-------------|
| [DigitrafficClient](DigitrafficClient.md) | REST poller — bootstraps and 60 s-resyncs the full fleet from `train-locations/latest`, plus metadata/status/routes from `/live-trains`. Owns the `TrainListModel`. |
| [DigitrafficMqttClient](DigitrafficMqttClient.md) | Live MQTT-over-WebSocket stream of position deltas; upserts into the shared `TrainListModel` and re-emits per-train timetable messages. |
| [MqttCodec](MqttCodec.md) | Header-only `mqttwire` namespace — minimal MQTT 3.1.1 wire encoders/decoders (CONNECT/SUBSCRIBE/PUBLISH parsing). |
| [TrainDetailsService](TrainDetailsService.md) | On-demand detail backend — fetches a run's timetable + carriage composition, streams live updates, drives the detail panel. |

## Models (QML-bound)

| Class | Description |
|-------|-------------|
| [TrainListModel](TrainListModel.md) | `QAbstractListModel` of live trains; merges REST + MQTT, derives bearing, map-matches fixes, computes the status ring. |
| [TrackListModel](TrackListModel.md) | `QAbstractListModel` of visible track segments; diffs viewport changes for incremental map updates. |
| [TimetableModel](TimetableModel.md) | `QRangeModel` over `TimetableStop` gadgets — one stop per row, with derived journey progress. |
| [TimetableFilterModel](TimetableFilterModel.md) | `QSortFilterProxyModel` hiding non-stopping timing points so the timetable `ListView` virtualises. |
| [CompositionModel](CompositionModel.md) | `QRangeModel` over `CompositionVehicle` gadgets — the carriage-order strip. |

## Track geometry & routing core

| Class / File | Description |
|--------------|-------------|
| [TrackService](TrackService.md) | GUI-facing owner of the baked rail network; provides viewport-filtered render geometry and implements `TrackMatcher` (Tier-1/Tier-2 matching). |
| [RailGraph](RailGraph.md) | Pure, Qt-Core-only Tier-2 core — geometry + topology + station crosswalk; Dijkstra routing, chainage-windowed projection, platform snapping. Unit-tested. |
| [TrackMatcher](TrackMatcher.md) | Abstract map-matching interface (plus the `TrackMatch` / `RouteMatchRequest` value types) decoupling `TrainListModel` from `TrackService`. |
| [Projection](Projection.md) | Header-only `tm35fin` namespace — EPSG:3067 ⇄ WGS84 datum conversion and the shared tangent-plane segment-matching helpers. |

## Architecture at a glance

```
                 REST snapshot              MQTT deltas
              DigitrafficClient ───┐   ┌── DigitrafficMqttClient
                                   ▼   ▼
                            TrainListModel ──► map markers (MapItemView)
                                   │  ▲
                       map-match   │  │ matcher (TrackMatcher)
                                   ▼  │
                             TrackService ──► TrackListModel ──► track layer
                                   │
                                RailGraph (routing/projection core)
                                   ▲ tm35fin (Projection.h)

   marker click ──► TrainDetailsService ──► TimetableModel ─►(TimetableFilterModel)─► panel
                          │            └──► CompositionModel ──────────────────────► carriage strip
                          └── live updates ◄── DigitrafficMqttClient.trainMessage
```

> AI assistance has been used to create this output.
