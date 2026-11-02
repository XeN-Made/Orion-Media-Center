# Validating on the console

Pictures rendered on a PC prove a design looks right. Only the console proves
it runs: that the shaders compile there, the frame rate holds, the sound
plays and the app closes cleanly. The app has a self-driving run mode for
exactly that, so a validation takes one launch and no controller.

## The tour run

Two facts about a title's sandbox shape the mechanism:

- It has no `/data`. The app's output (log, pictures, report) goes to its own
  storage, `/download0/hui/dev`, which exists only while the title runs. A PC
  can *read* it over FTP as `/mnt/sandbox/<TITLE_ID>_000/download0/hui/dev`,
  but cannot write there.
- The install folder is writable from a PC and the app sees it as `/app0`.
  So requests travel that way: `/data/homebrew/<TITLE_ID>/dev/request.txt`
  holds one line, `tour <design id|all> <token>` or `quit - <token>`. The app
  honours a request once per token (it remembers the last one), so a request
  left in place does not make every later launch a tour.

When the app finds a tour request it has not honoured yet, it:

1. shows every design in turn and replays the inputs its `tour()` describes;
2. saves a small picture (480 x 270) of each tour state to
   `dev/tour/NN-<id>[-<state>].bmp`;
3. measures every presented frame per design;
4. writes `dev/tour/report.txt` and logs the same lines;
5. stays up for four more minutes, because closing unmounts the storage and
   the evidence with it;
6. asks the system to close it (`sceSystemServiceLoadExec("exit", NULL)`).

`dev/app.log` is the app's log. Put the request in place **before** launching:
the tool does not write into the install folder of a running title.

The design id `all` tours every design; one id (`aurora`) tours only that one.

The report has one line per design:

```
tour 01 aurora       frames=412 avg_ms=16.67 worst_ms=16.92 draws=14 shapes=373
```

`avg_ms` near 16.67 with a `worst_ms` under about 20 means the design held
60 frames per second through its whole tour, including dialogs and blur.

## Running it

`tools/console-tour.py` does the whole cycle from a PC on the same network.
It needs FTP (2121), the kernel log (3232), an ELF loader (9021) and
[ShadowMount Plus](https://github.com/drakmor/ShadowMountPlus) on the
console, and the launch helper from
[ps5-homebrew-dev-protocol](https://github.com/blackbearreloaded/ps5-homebrew-dev-protocol).

```bash
make                                            # build dist/<TITLE_ID>
PS5_HOST=<console address> tools/console-tour.py results/run-1
```

It uploads the build (every file read back and compared by hash), writes the
trigger, waits until the console has registered the title, records the kernel
log, launches the title, waits for the app's own exit, downloads the report,
the pictures and `app.log`, and checks the log for errors. It never kills the
app and never retries.

A title copied to `/data/homebrew` for the first time is not known to the
console yet. ShadowMount Plus registers it once the folder has stopped
changing (about ten seconds), which creates `/user/app/<TITLE_ID>/mount.lnk`;
the tool waits for that file. Launching earlier only shows the system's
"title not found" screen: nothing starts and nothing is harmed, but the run
is wasted.

What to look at afterwards, in `results/run-1/`:

| File | Check |
| --- | --- |
| `report.txt` | Every design present; `avg_ms` about 16.67; no outlier `worst_ms` |
| `tour/*.bmp` | Compare with `build/snapshots/*.png` from the PC: same layout, colours and text |
| `app.log` | `gl 4.6`, `sounds files=67 rejected=0`, no `fatal`, `shader ... failed` or `rejected` lines; `tour finished`; `quit requested` |
| `klog.txt` | The title's start and its clean exit; no crash report |
| `shadowmount.txt` | The title's lines from the console's lifecycle log: registered, started, released |

## Checking ORION on a console

`tools/console-check.py` asks the ORION already installed (`make deploy`)
to check itself, without uploading a build or reinstalling it. It writes the
request `check <design id|-> <token>` to `/data/orion/dev-request.txt` (not
to the install folder, which the console would register again), starts
ORION through its own launch helper unless it is running, and reads the
report from `/data/orion/selftest/report.txt`. That file stays after ORION
closes, and a copy goes to ORION's own storage for a console where `/data`
cannot be written.

```bash
PS5_HOST=<console address> tools/console-check.py results/check-1           # checks only
PS5_HOST=<console address> tools/console-check.py results/check-1 library   # then the library's tour
PS5_HOST=<console address> tools/console-check.py results/check-1 --quit    # and close ORION after
PS5_HOST=<console address> tools/console-check.py results/check-1 --installs # and the installer
PS5_HOST=<console address> tools/console-check.py results/check-1 --pictures # and backgrounds
```

The checks (`src/orion/self_test.cpp`), one `PASS`, `FAIL` or `SKIP` line each:

| Check | Fails when |
| --- | --- |
| `data-writable` | ORION cannot write and read back `/data/orion` (the "cannot reach /data" report) |
| `payloads`, `elf-loader` | a payload is missing from the install, or nothing answers on port 9021 |
| `library` | the library found no titles; the line counts games, homebrew, emulated games and payloads |
| `drives` | an extended-storage or USB drive has games in its `user/app` and the library has none of them |
| `network-drive <address>` | a saved network drive does not sign in; the line says why |
|  | the network search (NetBIOS and WS-Discovery) finds no machine at all; the saved drives' machines it missed are named, since a Windows PC answers only while its network is Private |
| `console-share` | sharing is on, and the payload reports a problem or a sign-in to `\\127.0.0.1\data` fails |
| `share-list` | the console's share list (what Windows' `net view` reads) has no `data` |
| `drive-errors` | a wrong password, or a share that is not there, is let in or does not come back with the hint a player is shown |
| `memory` | the heap has refused an allocation |
| `pc-reaches-share` | sharing is on and port 445 does not answer from the PC |
| `install ok <file>` | with `--installs`: the test game installed off the share differs from what was packed, file by file |
| `install twice <file>` | the same game installed again is not refused as already there |
| `install stop <file>` | Stop half way leaves files behind |
| `picture <file>` | with `--pictures`: a picture a background takes does not decode (all its frames), or one it must refuse is not refused; the line has its size and time |

`--installs` packs an invented game with 7-Zip and WinRAR (Windows' own,
through WSL; `SEVEN_ZIP` and `RAR` name others): a game folder, ZIP with
the game in a folder, 7z, 7z volumes, stored and compressed RAR5, both in
volumes too, an image, and a large ZIP for Stop. About 200 MB go to
`/data/orion/selftest/fixtures`; ORION installs each off its own share
(sharing must be on) into `/data/orion/selftest/installed`, compares, and
removes it; the tool deletes the fixtures afterwards. RAR4 volumes
(`x.rar`, `x.r00`) are not among them: WinRAR 7 no longer writes RAR4.

ORION stays open afterwards unless `--quit` is given; after a tour it closes
itself a few minutes later, as with `console-tour.py`. The exit status is 1
when a check failed. Ask before every run, as for any console run: it starts
ORION. What the checks cannot see is what a person has to: the system's
installer prompts, sound, the controller's feel, and other people's
firmware and jailbreak combinations.

## Rules for console runs

These come from hard experience with shared development consoles:

- **Finish on the PC first.** Tests and snapshots must pass before a console
  is involved.
- **One run at a time, a handful per session.** Long unattended series of
  launches are how consoles get lost. Check that the console still answers
  between runs and stop at the first anomaly.
- **Let the app end itself.** The tour exits through the system. Do not send
  a kill to a title that is still rendering.
- **Take the console's lock** if it is shared, and release only your own.
- **Verify what you uploaded** after a pause: files written just before an
  unclean shutdown can come back empty.
- **Never touch the console's settings**, and never run anything that was not
  declared for the test.

## What a PC cannot tell you

State these as "not verified" until someone has checked them by hand:

- how the sounds actually sound through a TV, and their balance against music;
- how the controller rumble feels, and the light bar colour;
- how focus movement feels on a real stick (dead zone, repeat rate);
- text legibility from the sofa on the actual TV.
