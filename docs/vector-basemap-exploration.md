# Vector basemap exploration (2026-09-08)

> **Status: blocked upstream, no code changed.** The decision was taken to move
> the basemap from Esri raster tiles to CARTO vector tiles. The tile source and
> its licensing check out; the *renderer* does not. `maplibre-native-qt` v4.0.0
> (unreleased) builds against our Qt 6.11.2 and its QtLocation plugin loads and
> attaches, but renders nothing on either graphics backend on Apple Silicon —
> Metal is explicitly unsupported by the bindings, and the OpenGL path throws on
> the first vertex-buffer upload. See [The MapLibre spike](#the-maplibre-spike).
>
> Nothing in `qml/`, `src/` or `CMakeLists.txt` was touched. The whole spike ran
> in a scratch directory and is reproducible from
> [Appendix: reproduction](#appendix-reproduction).

Question asked: can the map be converted to a vector map, and if so how?

Verified on: macOS 26.6.2 (Darwin 25.6.0), Apple M5, Homebrew Qt 6.11.2 (arm64),
CMake 4.4.3 + Ninja, on a clean `main` at `120d34c`.

## What is already vector

Nearly all of it. Every feature the app draws is a QtLocation map item on the Qt
Quick scene graph, not an image:

| Layer | Type | Source |
|---|---|---|
| Rail network | `MapPolyline` via `MapItemView` | [`qml/Main.qml:285`](../qml/Main.qml) |
| Selected route overlay | `MapPolyline` | [`qml/Main.qml:308`](../qml/Main.qml) |
| Raw-fix connector / ring | `MapPolyline`, `MapQuickItem` | [`qml/Main.qml:322`](../qml/Main.qml) |
| Weather chips | `MapQuickItem` via `MapItemView` | [`qml/Main.qml:363`](../qml/Main.qml) |
| Stations | `MapQuickItem` via `MapItemView` | [`qml/Main.qml:392`](../qml/Main.qml) |
| Trains | `TrainMarker` via `MapItemView` | [`qml/Main.qml:421`](../qml/Main.qml) |

The single raster surface is the **basemap**: Esri `World_Light_Gray_Base` /
`World_Dark_Gray_Base` PNG tiles, fetched through Qt's `osm` geoservices plugin
pointed at the embedded provider manifests in `resources/basemap/{light,dark}/street`.

So "convert to a vector map" reduces to one question: replace the basemap.

## What the raster basemap costs today

1. **A theme flip rebuilds the whole `Map`.** [`qml/Main.qml:128-157`](../qml/Main.qml)
   destroys and recreates the `Plugin` + `Map` through a `Loader` on
   `isDarkChanged`, because the `osm` plugin resolves its providers only at
   construction. That drags along the `savedCenter*`/`savedZoom`/`everBuilt`
   dance to preserve the view across the rebuild, and a separate disk cache per
   style (`cacheDirFor()`) so the two grounds cannot poison each other.
2. **No HiDPI.** [`qml/Main.qml:187`](../qml/Main.qml) records why: with
   `osm.mapping.highdpi_tiles` Qt looks for a `street-hires` manifest, and Esri's
   MapServer has no `@2x` endpoint. The ground is soft on Retina while every
   polyline over it is crisp.
3. **`Theme` does not own the basemap colours.** [`qml/Theme.qml:88-146`](../qml/Theme.qml)
   is a run of hand-tuned contrast ratios measured *against a ground we cannot
   change* — including `ringVeryLate` being lifted `#F25555` → `#F36868` when the
   basemap moved to Esri Dark Gray. Every future basemap change re-opens that.
4. **Network dependency and third-party attribution** on every cold start.

A vector basemap fixes 1–3 outright: styles are data, so light/dark becomes a
`activeMapType` switch with no rebuild, geometry is resolution-independent, and
the style JSON is ours to recolour.

## Options considered

### A. MapLibre Native Qt + hosted vector tiles — *chosen, then blocked*

Swap `Plugin { name: "osm" }` for `Plugin { name: "maplibre" }` and hand it style
URLs. Every existing overlay keeps working untouched, because they are QtLocation
map items and the plugin is a QtLocation geoservices plugin. This is the only
mature route to real MVT rendering inside a `Map`; Qt Location itself has no
vector tile support and dropped the Mapbox plugins in Qt 6.

### B. Bake our own basemap over `itemsoverlay` — *the fallback*

We already do exactly this for rails: `scripts/bake_rails.py` →
`resources/rails.geojson.qz` (2.4 MB) → `RailGraph` → viewport-culled
`MapPolyline`s through a spatial grid, ~10k segments and 211k coordinates
nationally, with the culling machinery in `src/TrackService.cpp`.

Add a second baked blob — coastline, lakes, major roads, urban areas for Finland
from Natural Earth or an OSM extract — render it as `MapPolygon`/`MapPolyline`
through the same path, and switch the plugin to **`itemsoverlay`**, Qt's official
empty-map plugin (already deployed in our bundle). No tile fetch at all.

- **Wins:** fully offline, keyless, no new dependency, resolution-independent,
  and `Theme` finally owns every colour on screen — which deletes costs 1–4 above
  in one move.
- **Loses:** no place labels. Note this is *not* a regression:
  `World_Light_Gray_Base` is Esri's label-free base layer, labels live in a
  separate Reference layer we never load. If labels are wanted later, the
  declutter heuristic already in `TrainMarker` is the seed.

### C. Custom MVT decoder

Fetch `.mvt`, decode the protobuf, draw with Qt Quick Shapes. Rejected: large,
and it reimplements what B gets for free at our scale.

## CARTO as the tile source

We left CARTO in `6360da4` because it began watermarking keyless raster tiles.
That is still true, and it is *raster-only*.

**Verified 2026-09-08:**

- `basemaps.cartocdn.com/light_all/7/74/37.png` returns HTTP 200 stamped
  diagonally with "API KEY REQUIRED / carto.com/basemaps/apikey".
- `tiles-a.basemaps.cartocdn.com/vectortiles/carto.streets/v1/7/74/37.mvt`
  returns HTTP 200, `application/x-protobuf`, 129 KB — **no key, no watermark**.
- `positron-gl-style/style.json` is style spec v8, 93 layers, one vector source
  (`carto.streets/v1`, z0–14), glyphs and sprites on `tiles.basemaps.cartocdn.com`.

Style URLs, replacing the raster style ids we used to use:

| Style | URL | Old raster id |
|---|---|---|
| Positron | `…/gl/positron-gl-style/style.json` | `light_all` |
| Dark Matter | `…/gl/dark-matter-gl-style/style.json` | `dark_all` |
| Voyager | `…/gl/voyager-gl-style/style.json` | `rastertiles/voyager` |

Positron and Dark Matter are the grounds this palette was *originally* tuned
against — `qml/Theme.qml:146` still measures cargo navy against Dark Matter — so
going back to them realigns most of that work rather than forcing another round.

### Terms, and one contradiction worth recording

[carto.com/legal/basemap-terms](https://carto.com/legal/basemap-terms) describes
a free tier for anyone holding an API key: 5,000,000 tile requests per calendar
month aggregated across a customer's keys, commercial or not, suspendable "at any
time, in its sole discretion and for any or no reason". Attribution to both CARTO
and OpenStreetMap must stay prominent. Redistributing tiles, bulk downloading and
**server-side caching** are prohibited; a client-side disk cache is ordinary
client behaviour and is fine.

**The contradiction:** the `LICENSE.md` of CARTO's own style repository
(`CartoDB/basemap-styles`) still states that access to the basemap tile services
is "restricted to CARTO enterprise customers and Non-Profit GRANTS only and is
not available for free public use". The legal terms page is the newer and
authoritative source, and the free key-request flow works, so we proceed on that
basis — but it is written down here because the two disagree in CARTO's own
materials. The style *code* is BSD-3-Clause, so vendoring a copy of the style
JSON is permitted; the *tiles* are not ours to redistribute.

**Consequence for the key.** CARTO's public `style.json` does not currently
propagate a key into its source URLs — `?api_key=` and `?key=` both return the
identical unkeyed source. When the key requirement reaches vector, the mechanism
is therefore unknown. That argues for **vendoring our own copy of the style JSON**
in `qrc:` with the source `tiles` array written out explicitly, so a `?key=` can
be appended the day it is needed. Vendoring also lets `Theme` recolour the ground
and lets us drop layers we do not want (POIs), which is a rendering cost win.

## The MapLibre spike

Against `maplibre/maplibre-native-qt` at `ed7cc1d` (2026-08-24, `main`,
`VERSION.txt` = **4.0.0, marked unreleased in `CHANGELOG.md`**).

### What works

1. **It builds against Qt 6.11.2.** Their CI matrix already targets 6.11.2, and
   Homebrew's Qt ships the `Qt6LocationPrivate` module the plugin needs (94
   private headers under `QtLocation.framework/Headers/6.11.2`). Two deviations
   from their `macOS` preset are required:
   - `-DCMAKE_OSX_ARCHITECTURES=arm64` — the preset builds universal
     (`x86_64;arm64`), Homebrew's Qt is arm64-only, so the x86_64 slice fails to
     link (it surfaces as `QTest` link errors in the test targets).
   - `-DBUILD_TESTING=OFF`.
2. **The QtLocation plugin loads and attaches.**
   `Plugin.availableServiceProviders` → `maplibre,itemsoverlay,osm`;
   `isAttached: true`; no error string.
3. **The CARTO style registers as a map type** and `mapReady` goes true. Passing
   several URLs to `maplibre.map.styles` registers one `QGeoMapType` each
   (`src/location/qt_mapping_engine.cpp:129-177`), which is what would let a
   light/dark flip become an `activeMapType` switch with **no `Loader` rebuild**.
4. **The CARTO chain is reachable keyless.** A clean run caches `style.json`,
   `tiles.json` and both sprite resources in the plugin's ambient cache.

Useful plugin parameters found in `qt_mapping_engine.cpp`: `maplibre.map.styles`,
`maplibre.cache.directory`, `maplibre.cache.size`, `maplibre.client.name/version`
and `maplibre.items.insert_before` — the last would let our rails and trains be
inserted *beneath* the basemap's label layers. `maplibre.api.key` is **not**
useful for CARTO: it only feeds the built-in MapLibre/MapTiler/Mapbox provider
templates, so a CARTO key has to be carried in the style URL ourselves.

### What blocks it

**Metal backend — renders nothing.** The map paints only Positron's background
colour and requests **zero** vector tiles (`select count(*) from tiles` = 0 in the
ambient cache, while the style and sprites are cached). The cause is stated in
the project's own documentation, `docs/Usage.md:79`:

> Only OpenGL backend is supported for now!

This matters because Qt Quick on macOS defaults to Metal, and their own macOS
CMake preset defaults to `MLN_WITH_METAL=ON`.

**OpenGL backend — crashes on the first frame.** Rebuilt with
`-DMLN_WITH_METAL=OFF -DMLN_WITH_OPENGL=ON` and run with
`QSG_RHI_BACKEND=opengl`:

```
libc++abi: terminating due to uncaught exception of type std::bad_alloc

mbgl::gl::UploadPass::createVertexBufferResource(size=16) at upload_pass.cpp:40
mbgl::RenderStaticData::upload                     at render_static_data.cpp:15
mbgl::Renderer::Impl::render                       at renderer_impl.cpp:248
QMapLibre::MapRenderer::render                     at map_renderer.cpp:163
QMapLibre::TextureNodeOpenGL::render               at texture_node_opengl.cpp:75
QMapLibre::QGeoMapMapLibrePrivate::updateSceneGraph at qgeomap.cpp:144
QDeclarativeGeoMap::updatePaintNode
```

The `std::bad_alloc` is **not** memory exhaustion. `upload_pass.cpp:40` reads:

```cpp
MBGL_CHECK_ERROR(glBufferData(GL_ARRAY_BUFFER, size, data, ...));
if (glGetError()) {
    throw std::bad_alloc();
}
```

— a GL error rethrown as `bad_alloc`, on a **16-byte** static vertex buffer, the
very first upload of the frame. Reproduced identically under macOS's default
OpenGL 2.1 compatibility profile (what the `qml` runtime gets) and under a forced
3.3 core profile from a purpose-built C++ harness, so it is not a profile
mismatch. The likely root is that their macOS configure prints "Configuring
OpenGL **ES** backend" and macOS has no GLES.

### Conclusion of the spike

On Apple Silicon, v4.0.0-dev's QtLocation path renders on neither backend:
Metal is unimplemented in the bindings, OpenGL throws immediately. Since
"Full renderer backend support for Vulkan, Metal and OpenGL" is listed as a
v4.0.0 feature in their `CHANGELOG.md`, this is on the upstream path — it is not
finished.

**The blocker is the renderer, not CARTO.** Any MVT source — OpenFreeMap,
MapTiler, self-hosted — hits the identical wall. The CARTO decision, the key and
the terms analysis above all remain valid and can be picked straight back up.

## Where this leaves us

1. **File the upstream issue.** The backtrace above plus "Metal unsupported via
   the Location plugin; OpenGL ES backend on macOS throws at the first
   `glBufferData`; Qt 6.11.2, Apple M5" is a specific, actionable report.
   Revisit when v4.0.0 actually ships.
2. **Stay on Esri raster** in the meantime — it works, it is keyless, and it is
   already deployed.
3. **Or start option B**, the baked basemap, which needs no new dependency, no
   key and no upstream fix. Cheapest probe: extend the bake to emit a Natural
   Earth 10m coastline + lakes blob, render as `MapPolygon` over `itemsoverlay`,
   and judge it at zoom 4–9 before committing to roads and urban areas.

Trying the released v3.0.0 tag is **not** recommended: it targets Qt 6.5–6.7
against our 6.11 private headers, and it is the same macOS GL path that is
already crashing.

## Appendix: reproduction

```bash
git clone --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/maplibre/maplibre-native-qt.git mlq   # ~1.6 GB checked out
cd mlq
QT_ROOT_DIR=/opt/homebrew cmake --preset macOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_TESTING=OFF \
    -DMLN_WITH_METAL=OFF -DMLN_WITH_OPENGL=ON
QT_ROOT_DIR=/opt/homebrew cmake --build ../build/qt6-macOS --parallel
```

Then, against a QML file holding a `Map` with
`Plugin { name: "maplibre"; PluginParameter { name: "maplibre.map.styles"; value: "https://basemaps.cartocdn.com/gl/positron-gl-style/style.json" } }`:

```bash
QT_PLUGIN_PATH=<build>/src/location/plugins \
DYLD_FRAMEWORK_PATH=<build>/src/core:<build>/src/location:<build>/src/quick \
QSG_RHI_BACKEND=opengl \
qml probe.qml
```

Tile traffic is verifiable from the ambient cache set by
`maplibre.cache.directory`:

```bash
sqlite3 <cachedir>/maplibre.db "select count(*) from tiles;"
sqlite3 <cachedir>/maplibre.db "select substr(url,1,80), length(data) from resources;"
```
