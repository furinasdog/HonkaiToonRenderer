# Third-party notices

The renderer's original C++, Python and shader code is derived from HonkaiRendering,
copyright (c) 2026 HonkaiRendering contributors, and retains the MIT notice in LICENSE.

`Content/Materials/T_LinearWhite` is a linear-sampling copy of Unreal Engine's white
fallback texture. It retains the applicable Epic Games content terms. Unreal Engine
and any Engine-derived assets or code remain subject to their Epic license; the MIT
license of this repository does not replace those terms. An appropriately licensed
Unreal Engine installation is required to build and use this plugin.

No character model, model texture, game screenshot, reference illustration or captured
camera/video data is included or licensed by this plugin. Optional face and ramp maps
are provided by the host project and remain subject to their original usage terms.

Rendering references supplied for the original project:

- https://zhuanlan.zhihu.com/p/711037262
- https://zhuanlan.zhihu.com/p/120908528
- https://zhuanlan.zhihu.com/p/699682089

These are reading references. Their articles and images are not redistributed here.

## Optional NVIDIA DLSS integration

The original `FHonkaiUpscaling` API and adapter code are MIT. The separately obtained
NVIDIA DLSS SDK, official Unreal plugins, NGX libraries and DLLs retain their NVIDIA
copyrights and NVIDIA RTX SDK license terms. They are not included in this repository
and are not licensed under MIT by this project. Calling their public APIs does not
relicense the SDK. If a host distributes the optional production runtime binaries,
it must comply with their application distribution, attribution and commercial
release notification requirements and ship the applicable NVIDIA notices.
