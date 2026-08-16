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
| **Android SDK / NDK** | ✅ | Licenses accepted (user-approved 2026-08-16); `SetupAndroid.bat` installed platform android-34, build-tools 35.0.1, NDK 27.2.12479018, CMake 3.22.1, platform-tools, and set ANDROID_HOME / JAVA_HOME / NDKROOT user env vars. Verified on disk. |

Reminder that keeps proving true: **winget exit 0 ≠ installed** — every row
above was verified on the filesystem or by using the tool, not by trusting
the package manager's exit code.

Windows piping gotcha for posterity: `'y' | sdkmanager.bat --licenses` from
PowerShell does NOT reach the underlying Java process; use cmd's file
redirection (`cmd /c "sdkmanager.bat --licenses < yes.txt"`).

| **Temurin JDK 21** | ✅ | `C:\Program Files\Eclipse Adoptium\jdk-21.0.12.8-hotspot`, and JAVA_HOME points at it. Needed because Android Studio's bundled JBR is **Java 25**, which UE 5.8's pinned **Gradle 8.7** cannot run ("Unsupported class file major version 69"). Gradle 8.7 tops out at Java 21. |

## Android packaging — VERIFIED 2026-08-16

`RunUAT BuildCookRun -project=game/WorldSports.uproject -platform=Android
-cookflavor=ASTC -clientconfig=Development -build -cook -stage -pak -package`
produces `game/Binaries/Android/WorldSports-arm64.apk` (~166 MB dev build,
Vulkan + ES3.1 shader libraries, install/uninstall scripts, symbols).

Two first-run failures worth remembering:
1. NDK clang enforces `-Werror,-Wcomment`: a literal `/*` inside a doc
   comment (e.g. the path glob `schemas/*.py`) breaks the Android compile
   while MSVC stays silent.
2. The Java-25-vs-Gradle-8.7 mismatch above.

### `Build.bat` does NOT produce a runnable APK

`Build.bat WorldSports Android` compiles the `.so` and runs Gradle, and it
does leave an `.apk` in `Binaries/Android` — but that APK has no cooked
content. Installing it gets as far as a dialog reading:

```
Failed to open descriptor file ../../../WorldSports/WorldSports.uproject
```

Only `RunUAT BuildCookRun … -cook -stage -pak -package` produces something
that runs. Use `Build.bat` for the compile check (it is much faster, and it
is where the Android-only compile errors show up); use UAT to get an APK.

### UBT replays a CACHED environment

UnrealBuildTool captures env vars (`JAVA_HOME`, `ANDROID_HOME`, `NDKROOT`)
into `game/Intermediate/Build/<Platform>/…/Makefile.bin` and hands that
captured copy to child processes. A JAVA_HOME that was wrong once therefore
keeps failing the Gradle step *after* the real variable is fixed — and
`echo %JAVA_HOME%` in any shell shows the correct value the whole time.
Delete that `Makefile.bin` and `Intermediate/Android/UEBuildSettings.txt`,
which caches the same values. Object files survive; it is not a full rebuild.

## Android emulator

Installed 2026-08-17 for on-device verification without the physical phone:

```
sdkmanager --install emulator "system-images;android-34;google_apis;x86_64"
avdmanager create avd -n WSGames34 -k "system-images;android-34;google_apis;x86_64" -d pixel_6
emulator -avd WSGames34 -no-snapshot -gpu host -no-boot-anim
adb reverse tcp:8000 tcp:8000     # the backend, same as for a real device
```

The emulator is **x86-64** on a Windows host, so `bBuildForX8664=True` is now
set in `DefaultEngine.ini` alongside arm64. An arm64 APK will not install on
it, and an arm64 system image under full emulation is far too slow to judge
anything by. Packaging now produces both `WorldSports-arm64.apk` and
`WorldSports-x64.apk`.

**What the emulator can and cannot verify.** It is good for logic, flow, UI
layout, input routing and live server round-trips. It is *not* evidence for
the roadmap's frame-rate gate: the GPU is the host's through a translation
layer and there is no thermal behaviour at all. "Runs on a real mid-range
Android device at the tier's target frame rate" still needs the phone.

## Remaining: physical device

- `adb devices` sees the test phone (USB debugging on) — none connected yet.
- `game/Binaries/AndroidArchive/Install_WorldSports-arm64.bat` installs it;
  reaching the entry map on the device closes the roadmap's Phase 1 exit
  criterion.

## Current state of Phase 1 Track B

The Unreal project skeleton at `game/` (three modules, core subsystems, HTTP
auth client) **authenticates against the live backend** — the
`LiveBackend.AuthRoundTrip` automation test registers/logs in and round-trips
`/auth/me` against a running uvicorn. Win64 editor + runtime builds are
proven, 10 automation tests are green, and the Android APK packages end to
end. Only the on-device smoke remains.
