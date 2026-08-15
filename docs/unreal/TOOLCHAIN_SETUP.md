# Toolchain Setup

Status of the software the Unreal implementation phases depend on. Last
updated 2026-08-15.

## Installed and verified

| Software | Status | Notes |
|---|---|---|
| **Visual Studio Community 2022** | ✅ v17.14 | C++ desktop + game-dev workloads, MSVC 14.44, Win 10 SDK 10.0.22621. Verified via `vswhere` and by compiling the project. |
| **Epic Games Launcher** | ✅ | Used to install the engine. |
| **Unreal Engine 5.8.1** | ✅ | `C:\Program Files\Epic Games\UE_5.8` (Build.version 5.8.1, CL 56057345). The `game/` project compiles against it and its automation tests run. |
| **Android Studio** | ✅ | `C:\Program Files\Android\Android Studio` (winget, verified on disk — studio64.exe + bundled JBR). Needed for its JDK and as the SDK root convention. |
| **Android cmdline-tools** | ✅ | Bootstrapped into `%LOCALAPPDATA%\Android\Sdk\cmdline-tools\latest` from Google's official repo (no Studio first-run needed). |

Reminder that keeps proving true: **winget exit 0 ≠ installed** — every row
above was verified on the filesystem or by using the tool, not by trusting
the package manager's exit code.

## Blocked: Android SDK/NDK component install

`Engine\Extras\Android\SetupAndroid.bat` (UE 5.8 pins: platform android-34,
build-tools 35.0.1, NDK 27.2.12479018, CMake 3.22.1) stops at the **Android
SDK License Agreement** — a legally binding agreement with Google that has to
be accepted by a human, once:

```bash
"$LOCALAPPDATA/Android/Sdk/cmdline-tools/latest/bin/sdkmanager.bat" --licenses
```

Answer `y` to the prompts, then rerun
`"C:\Program Files\Epic Games\UE_5.8\Engine\Extras\Android\SetupAndroid.bat"`
(it is idempotent and sets ANDROID_HOME / JAVA_HOME / NDK_ROOT user env vars
itself).

## Verify before Android packaging

- `sdkmanager --list_installed` shows platform-tools, android-34,
  build-tools 35.0.1, ndk 27.2.12479018, cmake 3.22.1.
- `adb devices` sees the physical test phone (USB debugging on).
- The `game/` project packages a Development APK:
  `RunUAT BuildCookRun -project=game/WorldSports.uproject -platform=Android -cookflavor=ASTC -build -cook -stage -package`.
- The APK installs and reaches the entry map on the device — the roadmap's
  Phase 1 exit criterion, worth proving while the project is still nearly
  empty so packaging failures are diagnosed in isolation.

## Current state of Phase 1 Track B

The Unreal project skeleton exists at `game/` (three modules, core
subsystems, HTTP auth client) and **authenticates against the live backend**
— the `LiveBackend.AuthRoundTrip` automation test registers/logs in and
round-trips `/auth/me` against a running uvicorn. Win64 builds are proven;
Android packaging is the piece gated on the license step above.
