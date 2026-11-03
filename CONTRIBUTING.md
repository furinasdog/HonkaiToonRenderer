# Contributing

Build with a licensed Unreal Engine 5.8 installation. Enable the plugin and its
always-cook directory as described in README. An importer or capture service is
not required. The optional NVIDIA SDK must remain outside this repository.

Run `Automation RunTests HonkaiToon` in an Editor host. For rendering changes,
also cook a Development host and compare the same input, camera, light and
exposure on an actual GPU; NullRHI checks cannot validate pixels. Keep a repeat-run
control for temporal rendering noise. Report engine, GPU, driver and test scope.

Do not submit character models, model textures, game screenshots, camera footage,
private mappings or derived maps without explicit redistribution rights. Prefer
procedural fixtures and small focused regression checks. Preserve the original
MIT, Epic and optional NVIDIA notices; do not relabel third-party content as MIT.