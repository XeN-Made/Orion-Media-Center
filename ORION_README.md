# ORION Media Center

A launcher for a jailbroken PS5: your PS5 and PS4 games, homebrew, media apps
and emulated games in one library, with box art, favourites, tabs of your own
and a galaxy glass look. Everything is driven with the controller, and
everything can be changed in Settings.

Title ID: `PPSA99061`.

## What you need

- A PS5 you own, jailbroken, with a homebrew loader that runs folder apps
  from `/data/homebrew`.
- **ShadowMountPlus**, loaded after every jailbreak. It puts ORION (and your
  other folder apps) on the home screen and starts them.
- An **ELF loader on port 9021** (the usual one that comes with the
  jailbreak). ORION starts games and removes titles through a small helper it
  sends there.
- An **FTP server on the console** (port 2121, the common `ftpsrv`): to copy
  ORION over, and optional afterwards (ORION uses it to read the pictures
  the home screen shows behind a game).

Optional, for retro games: **RetroArch**, **Porpoise** (Dolphin for PS5:
GameCube and Wii) and **PS5SX2** (PS2), installed as folder apps too.

## Install

1. Download the ORION release ZIP and extract it. It holds one folder,
   `PPSA99061`.
2. With an FTP client, upload that folder to `/data/homebrew/` on the
   console, so you have `/data/homebrew/PPSA99061/eboot.bin`.
3. Wait about ten seconds: ShadowMountPlus registers the new title, and ORION
   appears on the home screen. If it does not, run ShadowMountPlus again.
4. Open ORION. The first time, a short setup walks you through the look,
   your emulators, box art, how the library is sorted and autoboot.

If an older `PPSA99061.ffpkg` or `.ffpfsc` image is in `/data/homebrew`,
delete it first: a folder and an image with the same title ID must not both
be there.

### Updating

Close ORION (hold Circle on the library, or close it from the PS button
menu), then upload the new `PPSA99061` folder over the old one. Your
settings, favourites and box art live in `/data/orion` and are kept.

The home screen keeps its own copy of the icon and backgrounds from the first
install. A release that changes them asks you to replace
`icon0.png`, `pic0.dds` and `pic1.dds` in `/user/app/PPSA99061/sce_sys/` and
`/user/appmeta/PPSA99061/` as well (and `/user/app/PPSA99061/icon0.png`).

## Open ORION after every jailbreak (autoboot)

Add `/data/homebrew/PPSA99061/assets/payloads/orion-autostart.elf` to the
payloads your jailbreak's autoloader sends. After the jailbreak it waits for
the browser (or whatever is in front) to close, then opens ORION. If you start
a game straight away instead, it stays out of the way.

To stop it without touching the autoloader: Settings, Library, **Autoboot ORION
after the jailbreak**. What it did is written to `/data/orion/autostart.txt`.

## Using it

| Button | What it does |
| --- | --- |
| Cross | Start the title |
| Square | Favourite (a star; Favourites collects them) |
| Triangle | Search |
| Options | The title's menu: move to a tab, play with another emulator, hide, remove |
| R3 | Settings |
| L1 / R1 | Change tab |
| L2 / R2 | Change system, on the Emulators tab |
| Up from the top row | The View and Sort pickers |
| Hold Circle | Close ORION |

- **Views**: Grid (covers), Cases (big game cases, three to a screen, with
  their system's band across the top) or List. Each tab can have its own.
- **Sort**: by name, recently added, platform, tag (Games, Homebrew, Media
  under separators), recently played or most played. Each tab can have its
  own.
- **Continue playing**: the titles you played last, over the All tab.
- **Tabs of your own**, a theme per tab, hidden titles: Settings, Tabs.
- **Removing a title**: Options, Remove. *Remove from home screen* takes it off
  the home screen and keeps its files; *Delete* erases its files (hold Cross
  to confirm). ORION closes while ShadowMountPlus does it, then opens again.

## Emulators and their games

Settings, Emulators, **Scan for emulators** finds RetroArch, Porpoise and
PS5SX2 and the games they can see:

- **RetroArch**: games in its own `content/<system>/` folders and every game in
  its playlists, wherever the files are.
- **Porpoise**: GameCube and Wii discs in its `porpoise/games` folder,
  `/data/porpoise/games`, `/data/games`, `/data/roms` and `/data/iso`.
- **PS5SX2**: PS2 discs (`.iso`, `.chd`, `.cso`, `.zso`) in `/data/PCSX2/games`,
  `/data/PCSX2`, and `PS2/` or `PCSX2/games/` on a drive.
- **Your own folders**: Settings, Emulators, an emulator, **Add a game folder**.

When two emulators can play a game, ORION asks which one the first time and
remembers (Options, Play with… changes it). PS2 games start straight in
PS5SX2; GameCube and Wii games straight in Porpoise; every other system in
RetroArch with the core you chose for it.

## Box art and music

- Box art downloads as the covers scroll into view and is kept in
  `/data/orion/art` (Settings, Box art, can move it to a USB drive). Covers
  PS5SX2 and Porpoise already downloaded are used as they are.
- Background music: every `.ogg` in `/data/orion/music` joins ORION's own
  tracks. Settings, Sound sets the volumes and skips a song.

## If something goes wrong

- **A game does not start**: Settings, Emulators shows what ORION found. What
  happened at each start is in `/data/orion/launch.txt` and
  `/data/homebrew/PPSA99061/launch-helper.txt`.
- **ORION does not open after rest mode**: restart the console (not rest
  mode) and jailbreak again; a jailbreak does not survive rest mode well.
- **Start-up**: each step is noted in `/data/orion/boot.txt`.
- **Start over**: delete `/data/orion/library.json` and ORION runs its setup
  again, keeping your box art.

Please include those files when you report a problem.
