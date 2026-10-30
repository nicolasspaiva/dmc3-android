# Devil May Cry 3 — native Android port

**English** | [Português](README.md)

A native port of **Devil May Cry 3: Special Edition** (the Xbox 360 *Devil May Cry HD Collection* version) to Android
phones. The game code was statically recompiled from PowerPC to ARM64 with
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp). Graphics, audio, videos, saves and controls are reimplemented
natively (OpenGL ES 3, SDL3, FFmpeg). There is no emulation.

> **You need to own the game.** The APK **contains no game files** (models, stages, sounds, videos). On first launch you
> copy the files from your own copy of the Xbox 360 *Devil May Cry HD Collection*.

Tested on a Samsung Galaxy S25 FE (60 fps). Requires Android 11 or newer, ARM64 and OpenGL ES 3.0.

> The app's own menus and messages are in Portuguese. This page gives each Portuguese label with its English meaning,
> for example **Tela** (Screen).

---

## Downloads: full or lite

The [Releases](../../releases) page has two versions. They differ only in the video cutscenes:

| | **DMC3-completo.apk** (full) | **DMC3-leve.apk** (lite) |
|---|---|---|
| Video cutscenes (intro, endings, pre-rendered scenes) | **play** | are **skipped** |
| APK size | ~32 MB | ~28 MB |
| Game files to copy | `DMC3.xex` + `Media/` **with** `Media/wmv/` | `DMC3.xex` + `Media/` **without** `Media/wmv/` |
| Total space on the phone | **~2.2 GB** | **~1.1 GB** |

Cutscenes made with the game engine (dialogue inside the stages) appear in both versions. Only the pre-rendered video
scenes differ. You can switch versions at any time by installing one over the other; saves and settings are kept.

---

## Installation

1. **Download and install the APK.** The first time, Android asks you to allow "install unknown apps" for your browser
   or file manager.
2. **Open the app and allow "All files access".** The app opens the system screen. Turn the option on and go back to the
   game. This permission is used **only** so the app can use the `DMC3` folder in internal storage (see
   [Saves](#saves)).
3. **Copy the game files** to `Internal storage/DMC3/`, from a PC (USB cable) or with the phone's file manager:

   ```
   DMC3/
   ├── DMC3.xex            ← the DMC3 executable (from the HD Collection disc)
   └── Media/
       ├── DMC3-0.zip
       ├── DMC3-1.zip
       ├── DMC3-2.zip
       └── wmv/            ← full version only: the cutscene videos
   ```

   While any file is missing, the app shows **what is missing** and a **Verificar de novo** (Check again) button.
4. **Done.** On the first complete launch, the app converts `DMC3.xex` into `DMC3.pe` (a few seconds, only once) and
   checks that it is the right version of the game. From then on, the game opens directly.

---

## Cutscenes

DMC3 has two kinds of scenes:

- **Game-engine scenes**: real-time 3D characters and environments. They always work, in both versions.
- **Pre-rendered video scenes**: the intro ("Capcom presents"), some scenes between missions and the extras. They are
  `.wmv` files (VC-1 video, WMA Pro audio) in the disc's `Media/wmv/` folder: 29 files, about 1.1 GB in total.

**How videos are played (full version):**

- When the game asks for a video, the port opens the matching file in `DMC3/Media/wmv/` with a native player (FFmpeg,
  with only the VC-1 and WMA Pro decoders, LGPL license).
- The video is drawn over the game at its original aspect ratio (with black bars when needed). Sound plays together with
  the picture.
- **Skipping** is decided by the game itself, as in the original: when it says stop, the player stops immediately.
- If the `wmv/` folder is missing, or a file is missing, that video is skipped and the game carries on normally.

**In the lite version** the video player is not in the APK. Every video is reported as finished right away, and the
`wmv/` folder is not needed. The story does not change: the game moves on to the next scene or stage.

**To add the cutscenes later:** install the full version over the lite one and copy the `wmv/` folder into
`DMC3/Media/`.

**Real-time cutscenes and sync:** the game advances its engine cutscenes one frame at a time, while voices and music
play in real time. On a phone that can't hold 60 fps in a heavy scene, the picture would fall behind the audio.
**Frame skipping** (see [the menu](#port-menu-gear-at-the-bottom-center)) keeps them in sync.

---

## Saves

The game uses the Xbox 360 save system. The port reimplements it by writing ordinary files:

```
DMC3/
└── saves/
    └── SaveGame/
        ├── save.bin      ← the game save (progress, orbs, styles, items...)
        └── .display      ← name shown on the load screen
```

- **Where it lives:** `Internal storage/DMC3/saves/`, an ordinary storage folder **outside** the app's private folder.
  That is why the app asks for "All files access".
- **Uninstalling does not delete it.** Android deletes an app's private folder when the app is uninstalled, but the
  `DMC3` folder keeps the saves and the game files. Reinstall and everything is back as it was.
- **Updating does not delete it.** Installing a newer version over the old one (or switching between full and lite)
  keeps saves and settings.
- **Backup:** copy the `DMC3/saves/` folder to a PC or the cloud. To restore, copy it back with the game closed.
- **Changing phones:** copy the whole `DMC3` folder (game + saves) to the new phone and install the APK.
- **When it saves:** at the game's usual points (save screen between missions, and menus). The *"Please do not turn off
  the Xbox 360 console..."* message is the original game's. Don't close the app while the saving icon is shown.
- **Compatibility:** `save.bin` uses the game's own format. A save made by the port on PC works on the phone and the
  other way round (just copy the `SaveGame` folder).

The **port settings** (screen mode, button layout, on-screen controls, controller layout, frame skipping) are stored
inside the app. They go back to the defaults if the app is uninstalled. Saves are not affected.

**Coming from an old test build?** Those kept everything in `Android/data/org.dmc3port/files`. On the first launch of
this version, the app moves the game files and saves from there into the `DMC3` folder by itself.

---

## Controls

- **On screen:** analog stick, A/B/X/Y, LB/RB/LT/RT, D-pad (style switch) and START/BACK. The round button at the top
  hides or shows the controls.
- **Bluetooth controller** (Xbox, PlayStation, Switch Pro and others): the on-screen controls hide by themselves when it
  is used. Buttons follow the **Xbox layout**: the bottom one jumps. The menu can switch to the **Nintendo letters**.
- **Real-time style switching** with the D-pad: ↑ Trickster, → Swordmaster, ← Gunslinger, ↓ Royalguard. The style
  name appears above the health bar.

### Port menu (gear at the bottom center)

- **Tela** (Screen): *preencher* (fill, stretched), *16:9* (black bars), *zoom* or **widescreen** (field of view
  widened to the phone's width, without stretching the characters).
- **Controles na tela** (On-screen controls): *visíveis* (shown) or *ocultos* (hidden).
- **Botões do controle** (Controller buttons): *posição Xbox* (Xbox positions) or *letras* (printed letters).
- **Pular quadros** (Frame skipping): *automático* (automatic, the default) or *desligado* (off). On a phone that can't
  hold 60 fps in a heavy scene, the app skips drawing some frames (at most every other one) so the game doesn't run
  slower than the clock. This keeps voices and music in real-time cutscenes in sync with the picture.
- **Editar layout dos botões** (Edit button layout): drag each button. Tap a button and use − / + to change its size,
  then **Pronto** (Done).
- **Restaurar layout padrão** (Restore default layout).

---

## Troubleshooting

| Symptom | What to do |
|---|---|
| "Arquivos do jogo não encontrados" (game files not found) | Check that the folder is `Internal storage/DMC3` (not `Download/DMC3`) and that the names match the list. |
| "O DMC3.xex é de outra versão do jogo" (DMC3.xex is from another version) | The port only works with the `DMC3.xex` from the Xbox 360 *HD Collection*. PS2, PC, PS3/PS4 or Switch versions won't work. |
| Cutscenes don't show | Expected in the lite version. In the full version, check the `DMC3/Media/wmv/` folder. |
| Audio out of sync in real-time cutscenes | Leave **Pular quadros** on *automático* in the gear menu. |
| The app closes on launch | Attach the `DMC3/dmc3_log.txt` file when opening an issue. |
| Slow performance | Attach `DMC3/dmc3_log.txt` to an issue: every 2 s it records fps and the time spent per frame (game, preparation, GL, present). |

---

## Building

Requires Windows, Python 3, CMake, Ninja, Visual Studio Build Tools + LLVM (clang-cl), Android SDK + NDK 28.2 and
JDK 21. The recompiled code is generated from **your own** `DMC3.xex` and is not in the repository.

1. `scripts/xex_extract.py x360/DMC3.xex x360/DMC3.pe`: generates the PE.
2. `tools/build_xenonrecomp.bat` and XenonRecomp with `x360/DMC3_recomp.toml`: generate `recomp/ppc/`.
3. `tools/build_ffmpeg_android.sh`: builds the minimal FFmpeg for Android (full version only).
4. In `android/`, run `gradlew assembleCompletoRelease -Pabis=arm64-v8a` (or `assembleLeveRelease`).

Technical details of the recompilation are in [RELATORIO_REA_DMC3.md](RELATORIO_REA_DMC3.md) (in Portuguese).

---

## Legal notice

Fan project, non-commercial and not affiliated with Capcom. *Devil May Cry* is a trademark of Capcom. No game files are
distributed here: to play, you need to own the Xbox 360 *Devil May Cry HD Collection*.
