# Building and Running on Android

Everything here has been run. Where a step has a trap, the trap is stated
with the symptom it produces, because each one cost real time to diagnose.

Toolchain versions and install state live in
[TOOLCHAIN_SETUP.md](TOOLCHAIN_SETUP.md).

---

## Prerequisites

| | |
|---|---|
| Unreal Engine | 5.8.1 at `C:\Program Files\Epic Games\UE_5.8` |
| JDK | Temurin **21** — `C:\Program Files\Eclipse Adoptium\jdk-21.0.12.8-hotspot` |
| Android SDK | platform-34, build-tools 35.0.1, platform-tools |
| NDK | 27.2.12479018 |
| Env vars | `JAVA_HOME`, `ANDROID_HOME`, `NDKROOT` — already set as user variables |

> **Java 21, not 25.** Android Studio's bundled JBR is Java 25 and UE 5.8's
> pinned Gradle 8.7 cannot run it: *"Unsupported class file major version
> 69"*.

---

## 1. Compile check (fast)

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" WorldSportsEditor Win64 Development -Project="C:\Users\riddl\Downloads\WorldSportsGames\game\WorldSports.uproject" -WaitMutex
```

Use this for iteration. It is much faster than packaging and it is where
Android-only compile errors surface first.

> **`Build.bat` cannot produce a runnable APK.** It builds one with no
> cooked content, which dies on launch with *"Failed to open descriptor
> file WorldSports.uproject"*. Only step 3 produces something that runs.

## 2. Run the tests

Offline automation (60 tests, no server needed):

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\riddl\Downloads\WorldSportsGames\game\WorldSports.uproject" -ExecCmds="Automation RunTests WorldSports;Quit" -unattended -nopause -nosplash -nullrhi -abslog=C:\temp\all.log -ForceLogFlush
```

Live-backend automation (7 tests) needs the backend running on
`127.0.0.1:8000`; swap `WorldSports` for `LiveBackend`.

Backend suite (227 tests):

```bash
cd backend && ./.venv/Scripts/python.exe -m pytest -q
```

## 3. Package an APK

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="C:/Users/riddl/Downloads/WorldSportsGames/game/WorldSports.uproject" -platform=Android -cookflavor=ASTC -clientconfig=Development -build -cook -stage -pak -package -nop4 -utf8output
```

Takes roughly 12–20 minutes cold. Produces both
`game/Binaries/Android/WorldSports-arm64.apk` (real devices) and
`WorldSports-x64.apk` (the emulator), because `bBuildForX8664=True`.

Two traps, both of which make a **failed** run look like a successful one,
because a stale APK is still sitting on disk from the last good build:

> **Do not set `JAVA_HOME` yourself.** It is already correct as a user
> variable. Setting it from a shell means typing the patch version from
> memory, and one wrong digit runs five minutes to the Gradle step before
> failing with *"JAVA_HOME is set to an invalid directory"* — long after
> the compile and cook have succeeded.

> **Do not edit sources while a package is running.** UAT compiles first
> and cooks second; an edit landing between the two fails the run with
> `error: null character ignored [-Werror,-Wnull-character]` in the
> engine's shared PCH and a generated `Definitions.*.h` — neither of which
> has a null character in it by the time the run ends. Rerun with no other
> change; do not hunt the null character.

**Always check the exit code and the APK timestamp**, not just that a file
exists:

```bash
ls -la game/Binaries/Android/WorldSports-x64.apk
```

## 4. Install and run

On the emulator:

```bash
adb install -r -d game/Binaries/Android/WorldSports-x64.apk
```

On a device (arm64):

```bash
adb install -r -d game/Binaries/Android/WorldSports-arm64.apk
```

Point the game at a local backend — needed on both:

```bash
adb reverse tcp:8000 tcp:8000
```

Launch (the package is `com.worldsports.game`, which is **not** the
directory name):

```bash
adb shell monkey -p com.worldsports.game -c android.intent.category.LAUNCHER 1
```

## 5. Emulator setup

```bash
sdkmanager --install emulator "system-images;android-34;google_apis;x86_64"
```

```bash
avdmanager create avd -n WSGames34 -k "system-images;android-34;google_apis;x86_64" -d pixel_6
```

```bash
emulator -avd WSGames34 -no-snapshot -gpu host -no-boot-anim
```

### Driving it

The in-game console command reports live state, which beats guessing:

```bash
adb shell "am broadcast -a android.intent.action.RUN -e cmd 'WSStatus'"
```

`adb logcat -d | grep WSStatus` then shows the app state, phase, clock,
distance and speed; `WSField` adds the event, attempt, bar height,
failures, metres to the board and the jump phase.

> **The emulator keeps running between adb commands.** Anything that pauses
> to read a screenshot loses the race — a 60 m gap opened between one
> reading and the next while a screenshot was being examined. A multi-step
> event has to be played by **one** script that holds, taps, polls and
> presses without returning.

> **A tap is a press *and* a release.** `adb shell input tap` during the
> pre-gun window is a false start. Use
> `input swipe X Y X Y <ms>` for a hold.

### What the emulator can and cannot verify

It has found **nineteen** defects that no offline or live-backend test
could see — every one of them the HUD or the world claiming something
untrue. Verify every event, HUD or world change on it.

It **cannot** judge frame rate. The 60 FPS target needs real hardware; the
last on-device run was a realme C73 5G on 2026-08-16, before nine of the
sixteen events existed.
