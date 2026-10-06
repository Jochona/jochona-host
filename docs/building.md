# Building
Sunshine binaries are built using [CMake](https://cmake.org) and requires `cmake` > 3.25.

## Building Locally

### Compiler
It is recommended to use one of the following compilers:

| Compiler    | Version |
|:------------|:--------|
| GCC         | 14+     |
| Clang       | 17+     |
| Apple Clang | 15+     |

### Dependencies

#### FreeBSD
> [!CAUTION]
> Sunshine support for FreeBSD is experimental and may be incomplete or not work as expected

##### Install dependencies
```sh
pkg install -y \
  audio/opus \
  audio/pulseaudio \
  devel/cmake \
  devel/evdev-proto \
  devel/git \
  devel/libevdev \
  devel/llvm19 \
  devel/ninja \
  devel/pkgconf \
  devel/qt6-base \
  ftp/curl \
  graphics/libdrm \
  graphics/qt6-svg \
  graphics/wayland \
  multimedia/libva \
  net/miniupnpc \
  ports-mgmt/pkg \
  security/openssl \
  shells/bash \
  www/npm-node22 \
  x11/libX11 \
  x11/libxcb \
  x11/libXfixes \
  x11/libXrandr \
  x11/libXtst
```

Use LLVM 19 when configuring a local FreeBSD build:

```sh
export CC=clang19
export CXX=clang++19
```

#### Linux
Dependencies vary depending on the distribution. You can reference our
[linux_build.sh](https://github.com/LizardByte/Sunshine/blob/master/scripts/linux_build.sh) script for a list of
dependencies we use in Debian-based, Fedora-based and Arch-based distributions. Please submit a PR if you would like to extend the
script to support other distributions.

##### KMS Capture
If you are using KMS, patching the Sunshine binary with `setcap` is required. Some post-install scripts handle this. If building
from source and using the binary directly, this will also work:

```bash
sudo cp build/sunshine /tmp
sudo setcap cap_sys_admin,cap_sys_nice+p /tmp/sunshine
sudo getcap /tmp/sunshine
sudo mv /tmp/sunshine build/sunshine
```

##### CUDA Toolkit
Sunshine requires CUDA Toolkit for NVFBC capture. There are two caveats to CUDA:

1. The version installed depends on the version of GCC.
2. The version of CUDA you use will determine compatibility with various GPU generations.
   At the time of writing, the recommended version to use is CUDA ~13.1.
   See [CUDA compatibility](https://docs.nvidia.com/deploy/cuda-compatibility/index.html) for more info.

> [!NOTE]
> To install older versions, select the appropriate run file based on your desired CUDA version and architecture
> according to [CUDA Toolkit Archive](https://developer.nvidia.com/cuda-toolkit-archive)

#### macOS
You can either use [Homebrew](https://brew.sh) or [MacPorts](https://www.macports.org) to install dependencies.

##### Homebrew
```bash
dependencies=(
  "boost"  # Optional
  "cmake"
  "doxygen"  # Optional, for docs
  "graphviz"  # Optional, for docs
  "icu4c"  # Optional, if boost is not installed
  "miniupnpc"
  "ninja"
  "node"
  "openssl@3"
  "opus"
  "pkg-config"
  "qtbase"
  "qtsvg"
)
brew install "${dependencies[@]}"
```

If there are issues with an SSL header that is not found:

@tabs{
  @tab{ Intel | ```bash
    ln -s /usr/local/opt/openssl/include/openssl /usr/local/include/openssl
    ```}
  @tab{ Apple Silicon | ```bash
    ln -s /opt/homebrew/opt/openssl/include/openssl /opt/homebrew/include/openssl
    ```
  }
}

##### MacPorts
```bash
dependencies=(
  "cmake"
  "curl"
  "doxygen"  # Optional, for docs
  "graphviz"  # Optional, for docs
  "libopus"
  "miniupnpc"
  "ninja"
  "npm9"
  "pkgconfig"
  "qt6-qtbase"
  "qt6-qtsvg"
)
sudo port install "${dependencies[@]}"
```

#### Windows

> [!WARNING]
> Cross-compilation is not supported on Windows. You must build on the target architecture.

First, you need to install [MSYS2](https://www.msys2.org).

For AMD64 startup "MSYS2 UCRT64" (or for ARM64 startup "MSYS2 CLANGARM64") then execute the following commands.

##### Update all packages
```bash
pacman -Syu
```

##### Set toolchain variable
For UCRT64:
```bash
export TOOLCHAIN="ucrt-x86_64"
```

For CLANGARM64:
```bash
export TOOLCHAIN="clang-aarch64"
```

##### Install dependencies
```bash
dependencies=(
  "git"
  "mingw-w64-${TOOLCHAIN}-boost"  # Optional
  "mingw-w64-${TOOLCHAIN}-cmake"
  "mingw-w64-${TOOLCHAIN}-cppwinrt"
  "mingw-w64-${TOOLCHAIN}-curl-winssl"
  "mingw-w64-${TOOLCHAIN}-doxygen"  # Optional, for docs... better to install official Doxygen
  "mingw-w64-${TOOLCHAIN}-graphviz"  # Optional, for docs
  "mingw-w64-${TOOLCHAIN}-miniupnpc"
  "mingw-w64-${TOOLCHAIN}-onevpl"
  "mingw-w64-${TOOLCHAIN}-openssl"
  "mingw-w64-${TOOLCHAIN}-opus"
  "mingw-w64-${TOOLCHAIN}-toolchain"
  "mingw-w64-${TOOLCHAIN}-qt6-static"
)
if [[ "${MSYSTEM}" == "UCRT64" ]]; then
  dependencies+=(
    "mingw-w64-${TOOLCHAIN}-MinHook"
    "mingw-w64-${TOOLCHAIN}-nodejs"
    "mingw-w64-${TOOLCHAIN}-nsis"
  )
fi
pacman -S "${dependencies[@]}"
```

Static Qt is enabled by default on Windows. Sunshine automatically adds the MSYS2 static Qt prefix at
`${MINGW_PREFIX}/qt6-static` when that package is installed. If an IDE does not inherit `MINGW_PREFIX`, Sunshine
derives the same prefix from the selected compiler. If static Qt is installed in a custom location, specify it with
`-DCMAKE_PREFIX_PATH=/path/to/qt6-static`.

To use dynamic Qt instead, configure with `-DSUNSHINE_USE_STATIC_QT=OFF` and ensure the dynamic Qt package is
available through the normal toolchain prefix.

To create a WiX installer, you also need to install [.NET](https://dotnet.microsoft.com/download).

For ARM64: To build frontend, you also need to install [Node.JS](https://nodejs.org/en/download)

### Clone
Ensure [git](https://git-scm.com) is installed on your system, then clone the repository using the following command:

```bash
git clone https://github.com/Jochona/jochona-host.git --recurse-submodules
cd jochona-host
mkdir build
```

### Build

```bash
cmake -B build -G Ninja -S .
ninja -C build
```

> [!TIP]
> Available build options can be found in
> [options.cmake](https://github.com/LizardByte/Sunshine/blob/master/cmake/prep/options.cmake).

### Package

@tabs{
  @tab{FreeBSD | @tabs{
    @tab{pkg | ```bash
      cpack -G FREEBSD --config ./build/CPackConfig.cmake
      ```}
  }}
  @tab{Linux | @tabs{
    @tab{deb | ```bash
      cpack -G DEB --config ./build/CPackConfig.cmake
      ```}
    @tab{rpm | ```bash
      cpack -G RPM --config ./build/CPackConfig.cmake
      ```}
  }}
  @tab{macOS | @tabs{
    @tab{DragNDrop | ```bash
      cpack -G DragNDrop --config ./build/CPackConfig.cmake
      ```}
  }}
  @tab{Windows | @tabs{
    @tab{NSIS Installer | ```bash
      cpack -G NSIS --config ./build/CPackConfig.cmake
      ```}
    @tab{WiX Installer | ```bash
      cpack -G WIX --config ./build/CPackConfig.cmake
      ```}
    @tab{Portable | ```bash
      cpack -G ZIP --config ./build/CPackConfig.cmake
      ```}
  }}
}

### Remote Build
It may be beneficial to build remotely in some cases. This will enable easier building on different operating systems.

1. Fork the project
2. Activate workflows
3. Push a commit to `main`, open a pull request, or run the *CI* workflow manually (`gh workflow run ci.yml --ref main`).
4. Download the artifacts/binaries from the workflow run summary

## CI on this fork

Jochona Host's GitHub Actions CI (`.github/workflows/ci.yml`) runs on push to `main`, on pull requests, on push of
a `v*` tag, and via `workflow_dispatch`. The vendored LizardByte `release_setup` action only understands push and
pull-request payloads (it reads the push event's `commits`) and fails with `KeyError: 'commits'` on manual runs,
or `IndexError` on tag pushes (which carry no new commits), so `ci.yml` skips it for `workflow_dispatch` and `v*`
tag pushes and generates local, non-publishing metadata instead.

This fork has none of LizardByte's signing/publishing secrets, so `release-setup` forces `publish_release=false`
outside of `LizardByte/` repositories. That has a few effects on this fork:

- The LizardByte-gated `release` job (which requires `publish_release == 'true'`) never runs, so CI never attempts
  to publish through LizardByte's own release channel.
- The `localize` and GH-Pages (`update-pages.yml`) workflows gate independently on
  `startsWith(github.repository, 'LizardByte/')` and are skipped here too — they need Crowdin/`GH_BOT_TOKEN` and
  GitHub Pages, neither of which this fork has configured.
- The macOS and Windows build jobs only use Apple/Azure signing secrets when `publish_release == 'true'`; with it
  forced false, those jobs build unsigned and don't need those secrets.

### Cutting a release

A fork-owned `fork-release` job (separate from the LizardByte-only `release` job above) publishes a real GitHub
Release when a `v*` tag is pushed:

```sh
git tag v1.0.0
git push origin v1.0.0
```

This triggers `ci.yml`, builds Windows (AMD64 + ARM64), macOS (arm64 + x86_64), Linux AppImage, and Linux Flatpak
(x86_64 + aarch64), then `fork-release` downloads those artifacts, writes a `SHA256SUMS` file, and creates a
GitHub Release on the tag with everything attached — all unsigned. `build-freebsd`, `build-archlinux`,
`build-docker`, and `build-homebrew` are intentionally left out of `fork-release`'s dependencies, so a slow leg
(FreeBSD aarch64 can take over an hour) or an upstream-only job never blocks publishing. The current pipeline
does not produce a `.deb`; only the packages `ci-linux.yml`/`ci-flatpak.yml` already build are published.

To dry-run the full build matrix without creating a release, trigger `workflow_dispatch` with the `publish` input
left at its default (`false`). Pass `publish: true` with an explicit `version` to publish a release from a manual
run instead of a tag push.

To get build artifacts without cutting a release, push to `main` or open a pull request and download the
per-platform artifacts from the workflow run summary (see [Remote Build](#remote-build) above).

<div class="section_buttons">

| Previous                              |                            Next |
|:--------------------------------------|--------------------------------:|
| [Troubleshooting](troubleshooting.md) | [Contributing](contributing.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
