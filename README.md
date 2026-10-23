# Instant Jump - Geode mod for Geometry Dash

- Mod ID: `khang58033.instant-jump`
- Developer: KYLZNX
- Built for: Geometry Dash **2.2081** (Windows) + Geode **5.11.2**

Jump from the exact frame you click. Includes input buffering, input queuing,
double-click fix, slope assist, jump prediction, and steadier ship / wave spam.

> Honest note: this source was written without being compiled or tested in-game.
> It uses Geode's public bindings (`GJBaseGameLayer::queueButton / handleButton`,
> `PlayerObject::update`, `PlayLayer::resetLevel`). If your Geode version has a
> slightly different signature, the compiler will say exactly which line to fix
> (see "If the build fails" below).

---

## What each feature does

| Feature | What it does | Setting |
|---|---|---|
| Instant click | Hands your press/release to the game as soon as the click event arrives, instead of letting it sit in the game's queue. | Enable Instant Jump |
| Input queuing | Only one press/release is applied per physics step. Extras are queued, so every tap lasts a full step. Makes Wave spam and ship / UFO tapping register every click, and keeps "straight fly" spam consistent. | Queue Inputs Per Step |
| Double-click fix | Drops a 2nd press that arrives faster than a human can click (bounce). | Double-Click Fix / Window |
| Buffering | A click shortly before landing is remembered and fires on touchdown (cube, robot, spider). | Input Buffering / Buffer Window / Buffered Clicks Kept |
| Slope assist | Adds extra buffer time on, and just after, slopes (the on-ground flag flickers there). | Slope Assist / Extra Window |
| Jump prediction | Buffer window grows with fall speed, up to a maximum bonus. | Jump Prediction / Max Prediction Bonus |

Notes
- "Jump prediction" is a heuristic based on fall speed, not a full physics simulation.
- The game still simulates in fixed physics steps; this mod removes queue delay and
  input loss, it does not rewind/re-simulate partial steps.
- Set **Buffer Window = 0** if a mid-air orb click ever causes an unwanted extra jump on landing.
- Some leaderboards and record rules treat input-assist mods as not allowed. Check before submitting records.

---

## HOW TO DOWNLOAD / INSTALL

### Step 0 - Install Geode (once)
1. Go to https://geode-sdk.org and download the installer.
2. Run it, select your Geometry Dash folder, install.
3. Start Geometry Dash once so Geode finishes setting up.

### Option A - Build online with GitHub (easiest, no programming tools needed)
1. Create a free account at https://github.com
2. Click **New repository** -> name it `instant-jump` -> Create.
3. Click **uploading an existing file**, drag in EVERYTHING from this folder
   (including the hidden `.github` folder) -> Commit.
   - Tip: if the `.github` folder will not drag in, use **Add file -> Create new file**,
     type `.github/workflows/build.yml` as the name and paste the contents of that file.
4. Open the **Actions** tab. The "Build Geode Mod" workflow runs automatically
   (or click it -> **Run workflow**). Wait a few minutes for the green check.
5. Open the finished run, scroll to **Artifacts**, download **Build Output**, unzip it.
6. Inside is `khang58033.instant-jump.geode`.

### Option B - Build on your own PC
Follow Geode's official setup for Windows first (it lists the exact compiler to install):
https://docs.geode-sdk.org/getting-started/

Then, in a terminal inside this folder:
```
geode sdk install
geode sdk install-binaries
geode build
```
(`geode sdk install` and `install-binaries` are only needed the first time.)
The result is `build/khang58033.instant-jump.geode`.

### Put the mod in the game
Copy `khang58033.instant-jump.geode` into:
```
<your Geometry Dash folder>\geode\mods
```
Steam default:
```
C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\geode\mods
```
(Quick way: Steam -> right-click Geometry Dash -> Manage -> Browse local files.)

Start Geometry Dash -> main menu -> Geode button -> **Mods** -> **Instant Jump**
-> settings (gear) to tune everything.

### If the build fails
- `geode` version error: open `mod.json`, change `"geode": "5.11.2"` to the version printed by `geode sdk version`.
- "no member named ..." or "no matching function ...": the Geode bindings changed.
  Look the function up at https://github.com/geode-sdk/bindings (search for `queueButton`,
  `handleButton`) and adjust the signature in `src/main.cpp`.
- Game crashes on start: remove the `.geode` file from `geode\mods`, then open an issue
  with the log from `<GD folder>\geode\logs`.

---

## Project layout
```
instant-jump/
  mod.json                      mod info + all settings
  CMakeLists.txt                build config
  src/main.cpp                  all mod code
  about.md  changelog.md        shown in the Geode mod page
  logo.png                      mod icon
  .github/workflows/build.yml   online build (Option A)
  .gitignore
  README.md                     this file
```
