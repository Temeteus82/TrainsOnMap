# [Windows][Vulkan] Crash in `createRendererWithQtVulkanDevice` when the Vulkan SDK is installed: Qt-side and mbgl TUs compile against different `vulkan.hpp`

## Summary

With the Vulkan SDK installed, a `Windows-Vulkan` build crashes on the first map
render. The crash is a call through a null `vkGetDeviceProcAddr` in
`VulkanRendererBackend::initDevice()`. All three test suites (`test_mln_quick`,
`test_mln_location`, `test_mln_widgets`) segfault the same way.

The cause is an ODR violation. `src/core/rendering/vulkan_renderer_backend.cpp`
picks up the **Vulkan SDK's** `vulkan.hpp`, while mbgl-core picks up the
**vendored** `vendor/Vulkan-Headers`. The two copies are different versions, so
`vk::detail::DispatchLoaderDynamic` has a different size in each translation unit.
That shifts every `mbgl::vulkan::RendererBackend` member after `dispatcher`:

- The Qt override `VulkanRendererBackend::initInstance()` writes `instance` at one
  offset.
- The base `RendererBackend::init()` (`renderer_backend.cpp:388`) reads it from
  another offset and gets a null `VkInstance`.

As a result, `dispatcher.init(instance.get())` loads no instance-level functions,
and the first device-level call crashes.

## Environment

- Windows 11 Pro 10.0.26200, AMD Radeon RX 6900 XT, AMD driver 26.9.2 (Vulkan 1.4.315)
- Qt 6.12.0 `msvc2022_64` (official kit), MSVC 19.44 (VS 2022 Build Tools), Ninja
- maplibre-native-qt `c3485f3` (main), maplibre-native submodule `702ebc6`
- Vendored Vulkan-Headers: `VK_HEADER_VERSION 352`
- Vulkan SDK 1.4.363.0 installed (`VULKAN_SDK` set): `VK_HEADER_VERSION 363`

## Steps to reproduce

```bat
call "...\VC\Auxiliary\Build\vcvars64.bat"
set QT_ROOT_DIR=D:\Qt\6.12.0\msvc2022_64
set VULKAN_SDK=D:\VulkanSDK\1.4.363.0
cmake --preset Windows-Vulkan -DBUILD_TESTING=ON
cmake --build --preset Windows-Vulkan --parallel
ctest --test-dir ..\build\qt6-Windows-Vulkan -C Release
```

Result: `0% tests passed, 3 tests failed out of 3`, all with SEGFAULT (`0xC0000005`).

## Stack (Release + `/Z7` and `/DEBUG`, `test_mln_location`)

```
00 0x0
01 QMapLibre!vk::detail::DispatchLoaderDynamic::init+0x20        [vendor/Vulkan-Headers/include/vulkan/vulkan.hpp @ 25866]
02 QMapLibre!QMapLibre::VulkanRendererBackend::initDevice+0xcd   [src/core/rendering/vulkan_renderer_backend.cpp @ 553]
03 QMapLibre!mbgl::vulkan::RendererBackend::init+0xd5            [vendor/maplibre-native/src/mbgl/vulkan/renderer_backend.cpp @ 395]
04 QMapLibre!QMapLibre::VulkanRendererBackend::init+0x1eb        [src/core/rendering/vulkan_renderer_backend.cpp @ 497]
05 QMapLibre!QMapLibre::VulkanRendererBackend::VulkanRendererBackend+0x106 [... @ 458]
06 QMapLibre!QMapLibre::MapRenderer::MapRenderer+0xbe            [src/core/map_renderer.cpp @ 97]
08 QMapLibre!QMapLibre::MapPrivate::createRendererWithQtVulkanDevice+0xb0 [src/core/map.cpp @ 1905]
09 QMapLibreQuickPrivate!QMapLibre::TextureNodeVulkan::render+0xd4 [src/quick/texture_node_vulkan.cpp @ 80]
0a QMapLibreLocation!QMapLibre::QGeoMapMapLibrePrivate::updateSceneGraph+0x635 [src/location/qgeomap.cpp @ 151]
0b Qt6Location!QDeclarativeGeoMap::updatePaintNode+0xed
...
```

At the crash, the dispatcher contains only the global entry points
(`vkGetInstanceProcAddr`, `vkCreateInstance`, `vkEnumerateInstance*`). Every
instance-level pointer is null, including `vkEnumeratePhysicalDevices` and
`vkGetDeviceProcAddr`.

## What the debugger shows

- **Before the override runs.** On entry to `VulkanRendererBackend::initInstance()`,
  `m_qtInstance` is valid and `instance` is null, as expected.
- **After it returns.** Back in `RendererBackend::init()`, the base class loads
  `instance` from `[this+0x1A68]` and finds `0`. It then calls
  `DispatchLoaderDynamic::init(vk::Instance)` with a null handle (`rdx = 0`).
- **Later.** Viewed from the Qt-side frame (`initDevice`), `instance.m_value` is a
  valid `VkInstance`. The same field is read at a different offset depending on
  which TU is looking.
- **Which header is inlined.** The trace shows the dispatcher code inlined from
  `D:\VulkanSDK\1.4.363.0\include\vulkan\vulkan.hpp` in
  `VulkanRendererBackend::init` (`vulkan_renderer_backend.cpp:485`). It comes from
  `vendor/Vulkan-Headers/include/vulkan/vulkan.hpp` in `RendererBackend::init`.

## Why the headers differ

From `compile_commands.json`, these are the Vulkan include paths in command-line
order:

| TU | 1st Vulkan include | later |
|---|---|---|
| `src/core/rendering/vulkan_renderer_backend.cpp` | `D:\VulkanSDK\1.4.363.0\include` | `vendor/Vulkan-Headers/include` |
| `src/core/map_renderer.cpp` | `D:\VulkanSDK\1.4.363.0\include` | `vendor/Vulkan-Headers/include` |
| `src/quick/texture_node_vulkan.cpp` | `vendor/Vulkan-Headers/include` | `D:\VulkanSDK\1.4.363.0\include` |
| `src/widgets/map_widget.cpp` | `vendor/Vulkan-Headers/include` | `D:\VulkanSDK\1.4.363.0\include` |
| `vendor/.../src/mbgl/vulkan/renderer_backend.cpp` | `vendor/Vulkan-Headers/include` | `D:\VulkanSDK\1.4.363.0\include` |

The SDK path reaches `MLNQtCore` through `Qt6::Gui`, which is linked `PUBLIC`.
Its interface links `WrapVulkanHeaders::WrapVulkanHeaders` whenever CMake finds
the SDK. That puts the SDK path ahead of the vendored headers that `mbgl-core`
provides, which is linked `PRIVATE`.

## Confirmation

I reconfigured the same tree with
`-DVulkan_INCLUDE_DIR=<repo>/vendor/maplibre-native/vendor/Vulkan-Headers/include`,
so Qt's `WrapVulkanHeaders` resolves to the vendored copy. After rebuilding,
**all 3 suites pass** and a downstream Qt Location app renders correctly on
Vulkan.

This probably doesn't show up in CI because the Windows jobs don't install the
Vulkan SDK (so `WrapVulkanHeaders` isn't found) and run the
`Windows-Vulkan-*-no-tests` presets. Any machine whose Vulkan SDK headers
differ from the vendored version and sit outside the compiler's default include
directories should hit it.

**Linux with distro headers is not affected** (checked 2026-10-15: CachyOS, Qt
6.12.0, GCC 16.2.1, `vulkan-headers` 1.4.363 = `VK_HEADER_VERSION 363` vs. the
vendored 352, RX 6900 XT on RADV / Mesa 26.2.4, maplibre-native-qt `ed7cc1d`).
`WrapVulkanHeaders` resolves to `/usr/include`, but that is an implicit include
directory: CMake leaves it off the command line and GCC searches it last, so both
TUs pick up the vendored `vulkan.hpp` first. The `Linux-Vulkan` preset builds
with `-DBUILD_TESTING=ON` and no workaround, all 3 suites pass on the Qt RHI
Vulkan backend, and a downstream Qt Location app renders correctly.

## Suggested fix

Point Qt's `WrapVulkanHeaders` at the vendored headers before Qt is found. This
goes in the top-level `CMakeLists.txt`, just above `# Find Qt`:

```cmake
# Qt6::Gui exposes WrapVulkanHeaders (the Vulkan SDK when installed) ahead of mbgl-core's
# vendored Vulkan-Headers. vulkan.hpp layouts differ between versions (ODR violation, crash
# on first render), so pin Qt's lookup to the vendored copy before Qt is found.
if(MLN_WITH_VULKAN)
    set(Vulkan_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/vendor/maplibre-native/vendor/Vulkan-Headers/include"
        CACHE PATH "Vulkan headers, must match mbgl-core's vendored copy" FORCE)
endif()
```

This is the confirmed workaround below, built into the project. It is one change
in one place, and the SDK path then drops off every command line, so no target's
include order matters any more. `FORCE` is deliberate: any other value brings the
crash back. Adding the vendored path `BEFORE PRIVATE` on `MLNQtCore` alone would
also cover both affected TUs, but it leaves the SDK path on the command line, and
a later target that includes `vulkan.hpp` could break the same way.

**Tested 2026-10-22** on the environment above (maplibre-native-qt `c3485f3`): with
the fix applied, a fresh `cmake --preset Windows-Vulkan -DBUILD_TESTING=ON` with
`VULKAN_SDK` set gives `compile_commands.json` entries for all five TUs in the
table with only `vendor/Vulkan-Headers/include`, and **all 3 suites pass**.

Workaround until fixed: configure with
`-DVulkan_INCLUDE_DIR=<repo>/vendor/maplibre-native/vendor/Vulkan-Headers/include`.
