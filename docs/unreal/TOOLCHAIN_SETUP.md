# Toolchain Setup

Status of the software the Unreal implementation phases depend on, and the
one step that needs your account. Checked/updated 2026-08-10.

## Installed automatically

| Software | Status | Notes |
|---|---|---|
| **Visual Studio Community 2022** | ✅ Installed — v17.14 | With C++ desktop + game-dev workloads, MSVC x64, Windows 11 SDK. Verified via `vswhere`. |
| **Epic Games Launcher** | ✅ Installed | `EpicGamesLauncher.exe` present under `C:\Program Files\Epic Games\Launcher\`. Ready to sign in. |

Both were installed with `winget`. Note: winget's own exit code returned 0
before the real installers finished — the actual completion was confirmed
separately by watching the setup processes and querying `vswhere`, not by
trusting that exit code.

## Needs YOUR account — I cannot do these

Unreal Engine is distributed through the Epic Games Launcher, which requires
signing in to an Epic account. Entering credentials is yours to do, not mine.

1. **Launch the Epic Games Launcher** and sign in (or create a free Epic
   account). If Unreal will publish to consoles later, link accounts then;
   for Android none of that is needed.
2. **Unreal Engine tab → Library → "+" → Install.** Pick the latest stable
   5.x. The brief asked for 5.8 — the launcher will show whether that version
   is offered; if not, take the current stable release and we pin to it
   (`MIGRATION_ANALYSIS.md` §0 flagged this).
   - In the install options, **tick "Android"** under Target Platforms so the
     mobile toolchain components come down with the engine.
   - Budget ~50–70 GB for the engine and expect a long download.
3. **Android SDK / NDK / JDK.** Unreal pins specific versions per engine
   release, so install them *through Unreal* rather than guessing:
   - Engine install dir → `Engine\Extras\Android\SetupAndroid.bat`.
   - This pulls the exact SDK/NDK/JDK the chosen engine version wants. Doing it
     any other way tends to produce version-mismatch packaging failures.
   Alternatively install Android Studio first and point Unreal at its SDK, but
   `SetupAndroid.bat` is the lower-friction path.

## Verify before Phase 1

Once the above is done, these confirm the pipeline is real:

- `vswhere -latest -property installationVersion` → returns a 17.x version ✅
  (already true)
- Epic Launcher lists an installed Unreal Engine.
- In the engine: **Edit → Plugins → search "Android"** shows the platform
  support present; **Platforms → Android** is not greyed out.
- A throwaway blank C++ project **compiles** (proves MSVC is wired to Unreal)
  and **packages a development APK** (proves the Android chain works end to
  end). This is the real gate — `DEVELOPMENT_ROADMAP.md` Phase 1 exit
  criterion — and it is worth doing with an empty project before any game
  content exists, so a packaging failure is diagnosed in isolation.

## Disk

674 GB free at check time — comfortably enough for VS (~20 GB) + Unreal
(~60 GB) + Android components (~15 GB) + project derived-data.

## Meanwhile

None of the backend track needed any of this, and it is done: the career
athlete, validated results, leaderboards, cloud save, tournaments, friendships
and analytics all ship and are tested against Postgres. When the engine is in
place, `DEVELOPMENT_ROADMAP.md` Phase 1 Track B (the Unreal project skeleton
authenticating against this backend) is the first step that consumes it.
