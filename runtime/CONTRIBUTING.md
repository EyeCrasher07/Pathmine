# Contributing to the Pathmine runtime

Follow the root [contribution guide](../CONTRIBUTING.md). This directory keeps
NVIDIA's renderer and platform adaptation together; its original copyright and
license notices remain in place.

The [C++ style guide](documentation/CONTRIBUTING-style-guide.md) and
[shader guide](src/dxvk/shaders/rtx/README.md) describe the renderer conventions.
The [native build and tests](native/README.md) cover Linux. Windows builds use
Meson and the scripts documented in [README.md](README.md).

Report Pathmine issues in the [project tracker](https://github.com/EyeCrasher07/Pathmine/issues).
When an issue reproduces in an unmodified NVIDIA release, link the upstream
report as well. Submit branch fixes as additional commits; preserve history.
