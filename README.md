# Honkai Toon Renderer

Reusable two-band character shading for Unreal Engine 5.8 deferred rendering. One
runtime module owns surface/outline material instances, authored toon-ramp packing,
face threshold maps and the toon postprocess. The host supplies ordinary UE mesh,
camera and texture objects. No PMX, VRM4U, tracking, Slate or external process dependency
is required.

The plugin builds in an independent UE 5.8.4 Editor and Development host without
an importer or tracker. Its eight generic assets pass actual graph and dependency
checks. All seven plugin automation groups pass under NullRHI with and without the
optional official NVIDIA SDK. Both production hosts pass their complete Editor
suites; a cooked Win64 Development host passes 15 runtime checks. Actual DX12
before/after extraction captures were visually compared, with differences near a
repeat-run control. These checks do not establish exact matching to a game's art.

Shipping packaging and actual GPU DLSS remain unverified at this checkpoint.
Character models, reference screenshots and private test recordings are excluded.

## Installation

Place the plugin in your project's `Plugins/HonkaiToonRenderer`, enable it in the
`.uproject` and add `HonkaiToonRenderer` to your module dependencies. Add this to the
host project's `Config/DefaultGame.ini`:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysCook=(Path="/HonkaiToonRenderer/Materials")
```

The host must use deferred rendering and these project settings in
`Config/DefaultEngine.ini`:

```ini
[/Script/Engine.RendererSettings]
r.CustomDepth=3
r.AllowStaticLighting=False
r.Substrate=False

[ConsoleVariables]
r.EarlyZPassOnlyMaterialMasking=0
```

Disabling static lighting preserves the GBuffer material-AO byte used for ramp
indices; with static lighting enabled UE's decode returns AO=1 instead. Base-pass
masking keeps coincident cutout overlays clipped. Disable scene AO/GI/reflections
for this direct-light pipeline.
`bApplyViewPreset=true` applies the tested manual exposure, neutral tone mapping,
bloom and scene AO/GI/reflection overrides only to the supplied camera. It does not
change project CVars or the host's anti-aliasing/upscaling settings.
If the host owns a dedicated showcase camera and needs the same view before loading
a subject, call `UHonkaiRenderInstance::ConfigureCamera(Camera)` once at creation.
This explicit helper permanently configures that camera; `Bind` only restores the
settings present when binding began.

## Runtime use

```cpp
#include "HonkaiRenderInstance.h"

// Keep this UObject in a UPROPERTY on the host actor/controller.
Renderer = NewObject<UHonkaiRenderInstance>(this);
FHonkaiRenderTargets Targets { SurfaceMesh, OutlineMesh, Camera };
FHonkaiRenderOptions Options;
Options.bApplyViewPreset = true;
FString Error;
if (!Renderer->Bind(Targets, SurfaceInputs, FaceMaps, Options, Error))
{
    // Existing bound subject is retained. Display/log Error in the host.
    return;
}
Renderer->SetLighting(WorldLightDirection, HeadCompensatedWorldLightDirection);
// On teardown, before destroying meshes/camera:
Renderer->Detach();
```

All calls run on the game thread. `SurfaceInputs` has one entry per material slot;
source colors and alpha are retained. Texture inputs are live, non-virtual 2D
textures. Resident streaming textures keep their existing resources; CPU mip
payloads are not required for those inputs. New resources are prepared before
material parameters are bound, and a failed preparation retains the old binding.
The optional outline mesh has the same slot
count and must follow the same current bone pose. Source `OutlineWidth` is in UE
units; face-map outlines use 75% of it, preserving the current renderer's nose-line
control. A missing/unsupported CPU toon ramp uses the shared fallback ramp without
changing the source texture. Supported CPU ramps are uncompressed BGRA8; the first
255 distinct valid ramps occupy atlas rows 1–255, row 0 is the fallback.

`FaceMaps` is a map from slot index to a linear (`SRGB=false`), clamped 2D control
texture. R/G contain opposing horizontal-light thresholds, B controls nose/face
outline width/color, A contains the small nose-shadow region. The host is responsible
for correct UV layout and validating any files it imports. Empty maps disable face
threshold shading and retain the outline variants present at Bind. Similarly, a
replacement affecting only some slots keeps the other slots' bind-time nose/outline
control. This preserves authored original outlines when an enhancement only covers
one face slot. Invalid replacements preserve current materials and maps.
`PrepareFaceMaps` validates the same slot/texture contract and prepares resources
without changing active maps, material parameters or visible outlines. A host may
use it to check a cached inactive enhancement before committing its local state.

Both light directions are in world space. The second may compensate current head
rotation; the material then performs world-to-local conversion. Pass the same vector
for both in a static showcase. Call after bone transforms finalize when using animated
face compensation.

`SetTint` changes a surface instance's color and opacity, retaining source textures.
It can support a material-selection preview. The host retains the original tint to
restore it. Vertex alpha is reserved for optional per-instance part visibility and
multiplies authored opacity only; it does not change RGB or vertex positions.

`Detach` restores saved material overrides, surface custom-depth/stencil and outline
visibility. Current host per-slot overlay materials are preserved. It removes only
its own camera blendable and restores only view-preset
fields it changed. Unrelated postprocess fields/blendables are retained. Do not have
another system take ownership of these same fields while the instance is bound.

## Authoring and verification

Run `Scripts/build_materials.py` inside an Unreal Python editor commandlet to generate
seven parent materials plus `T_LinearWhite` at `/HonkaiToonRenderer/Materials`.
`Scripts/verify_materials.py` checks actual alpha connections, skeletal/morph usage,
embedded shader code and package dependencies. Importing the build module alone does
not mutate assets. Python is an authoring dependency and is not used at runtime.

Run `Automation RunTests HonkaiToon` in Editor and non-Editor Development.
Then perform a real GPU/cooked Shipping smoke in each host; source and NullRHI checks
alone do not verify pixel output. Verify all eight plugin packages are in the cooked
registry and none depends on the original host's `/Game` packages.

## Current scope

One active subject per camera, using stencil value 1 and one AO-indexed ramp atlas.
The current implementation targets deferred desktop rendering with nonmetallic
surfaces, a neutral directional key light and the supplied view preset. Translucent
overlays preserve authored unlit color/alpha because they have no deferred GBuffer.
Multi-subject/camera composition, Forward/Mobile/Substrate and arbitrary lighting
setups have not been validated. Model importing, physics, face-map authoring, UI,
tracking and output streaming remain host responsibilities.

Original code is MIT. See THIRD_PARTY_NOTICES.md for Unreal Engine content terms.

## Optional NVIDIA upscaling

`FHonkaiUpscaling` is independent of the material binding instance. A host keeps one
owner for the application's global upscaling state and calls it on the game thread:

```cpp
#include "HonkaiUpscaling.h"
FHonkaiUpscaling Upscaling;
FString Error;
Upscaling.SetMode(EHonkaiUpscalingMode::DLAA, ViewportSizeInPixels, Error);
// Or DLSSQuality. TSR restores the values saved before the first successful enable.
Upscaling.SetMode(EHonkaiUpscalingMode::TSR, ViewportSizeInPixels, Error);
// EndPlay, before destroying the viewport:
Upscaling.Restore();
```

The default is TSR/disabled. Without the optional NVIDIA SDK, the plugin still
builds and NVIDIA modes return an unsupported reason without changing AA or
resolution. The SDK is detected only under the selected host project's
`Plugins/NVIDIA/DLSS` and `Plugins/NVIDIA/StreamlineNGXCommon` directories. Public
headers contain no NVIDIA types or includes. Host UI stays outside this plugin.

The tested integration target is NVIDIA's official UE5.8 DLSS4.5 plugin 8.9.0 with
NGX310.9.16. DLAA uses 100% resolution; DLSS Quality queries the official optimal
ratio. Both request preset M rather than the package's default K. These are requests,
not proof that a GPU feature executed: inspect the actual NVIDIA runtime log and
rendered output in the host. Current tests do not establish GPU image quality.

On successful enable the owner uses UE5.8 temporary CVar overrides for AA method,
screen percentage, temporal upscaler/upsampling, dynamic-resolution mode, preset and
DLSS enable. Removing that temporary layer restores the underlying values and
priorities, including constructor defaults, and retains later console overrides.
Changing the host's render scale while NVIDIA mode is active must first restore it.
Calls must follow PostEngineInit. Frame generation and ray reconstruction are not
enabled. Independent scene captures, including a host's Spout capture, do not
automatically use the main viewport's DLSS. Slate UI remains at output resolution.

The NVIDIA SDK is separately licensed and excluded from this MIT source repository.
Use the fixed official package and retain its notices. For redistribution, include
the applicable NVIDIA license documents and application attribution. Commercial
release notification requirements remain the application publisher's responsibility.
No SDK sources or binaries are included here.

After reviewing NVIDIA's license, prepare the fixed official package with:

```powershell
./Scripts/Prepare-NvidiaDlss.ps1 -ProjectRoot C:/Projects/YourHost -ArchivePath C:/Downloads/2026.10.16_UE5.8_DLSS4.5Plugin_v8.9.0.zip
```

Alternatively use `-Download` to obtain the package directly from NVIDIA. The host
root must contain exactly one `.uproject`; existing SDK modifications are preserved
and cause a clear refusal. Add `/Plugins/NVIDIA/` to the **host** `.gitignore` before
installing. The script verifies the fixed archive hash and NVIDIA production DLL
signatures, excludes vendor precompiled editor binaries and development DLLs, and
records source provenance. It never places the SDK in this plugin's Git repository.

### 0.1.1 shading refinement

Pale lit face paint receives a less magenta highlight shoulder. Low-saturation
surfaces retain more of the authored ramp hue instead of inheriting the warm
shadow tint used for pink paint. Body ramp shadows have a modest contrast and
chroma increase; the two-band boundary and authored sphere-map inputs remain.

The host validation uses official March 7th screenshots and checks Sparkle and
Phainon as additional models. These are local image comparisons, not a claim of
matching the original game's lighting, geometry or shading exactly. No character
assets or reference images are included.
