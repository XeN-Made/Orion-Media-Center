# Third-party notices

## Credits and acknowledgements

ORION Media Center exists thanks to the maintainers and contributors of:

- [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl), with
  [Mesa](https://mesa3d.org/) and OpenGNM PSBC, for OpenGL 4.6 on PS5;
- [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  and the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) for the
  reproducible native foundation;
- [Inter](https://github.com/rsms/inter),
  [Montserrat](https://github.com/JulietaUla/Montserrat),
  [DejaVu](https://dejavu-fonts.github.io/),
  [Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P) and
  [Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand) for the
  interface typefaces, and
  [stb](https://github.com/nothings/stb) for music decoding, font baking and
  PNG writing;
- LLVM/Clang, Python, zlib and GoogleTest for build, packaging and validation
  tooling;
- Inigo Quilez, whose published signed-distance functions the shape shader
  follows (rounded box, triangle, star and arc).

The rendering kit, the sound mixer and the sound effects come from
[ProsperoPuzzles](https://github.com/blackbearreloaded/ProsperoPuzzles) and
ProsperoEden, both by BlackBearReloaded.

ORION Media Center is built on ps5-homebrew-ui, Copyright (C) 2026
BlackBearReloaded, and is licensed under
GPL-3.0-or-later. It is built on `ps5-native-app-boilerplate`; the notices
below cover the boilerplate's dependencies and every third-party component the
project adds.

## Fonts and font baking

| Typeface | Files | Copyright | Licence |
| --- | --- | --- | --- |
| Inter Regular, SemiBold | `third_party/fonts/Inter-*.ttf` | (c) 2016 The Inter Project Authors | SIL Open Font License 1.1 (`Inter-LICENSE.txt`) |
| Montserrat Medium | `third_party/fonts/Montserrat-Medium.ttf` | (c) 2011 The Montserrat Project Authors | SIL Open Font License 1.1 (`Montserrat-LICENSE.txt`) |
| DejaVu Sans Mono | `third_party/fonts/DejaVuSansMono.ttf` | (c) 2003 Bitstream, Inc.; DejaVu changes are public domain | Bitstream Vera licence (`DejaVu-LICENSE.txt`) |
| Press Start 2P | `third_party/fonts/PressStart2P-Regular.ttf` | (c) 2012 The Press Start 2P Project Authors | SIL Open Font License 1.1 (`PressStart2P-LICENSE.txt`) |
| Patrick Hand | `third_party/fonts/PatrickHand-Regular.ttf` | (c) 2010-2012 Patrick Wagesreiter | SIL Open Font License 1.1 (`PatrickHand-LICENSE.txt`) |

`assets/fonts/*.huifont` are distance-field renderings of these typefaces
produced by `tools/bake-fonts.sh`; the licence texts ship beside them.

The baker uses [stb_truetype](https://github.com/nothings/stb)
(`third_party/stb/stb_truetype.h`, public domain or MIT) and the host snapshot
tool uses stb_image_write (`third_party/stb/stb_image_write.h`, same terms).
Both are host-only and are not linked into the PS5 application.

## Sound effects and music

`assets/audio/sfx/paper/*.wav` are the sound effects of ProsperoPuzzles and
`assets/audio/sfx/glass/*.wav` those of ProsperoEden. They were generated for
those projects with ElevenLabs Sound Effects v2 and prepared with
`tools/process-sfx.py` (trimmed, faded and levelled). No sound effect was
generated for this repository. `assets/audio/music/*.ogg` ("First Light",
"Open Strings" and "Quiet Hours") are the project's own background music,
supplied by its owner and encoded to 48 kHz OGG Vorbis at -18 LUFS. All are
Copyright (C) 2026 BlackBearReloaded and distributed under the project licence.

## Music decoding

Music is decoded by [stb_vorbis](https://github.com/nothings/stb) (public
domain or MIT; see `src/third_party/stb/LICENSE`), vendored at the commit in
`src/third_party/stb/UPSTREAM` by `tools/update-stb.sh`. It is linked into the
PS5 application.

A title's own music (its `snd0.at9`, ATRAC9) is decoded by
[LibAtrac9](https://github.com/Thealexbarney/LibAtrac9), Copyright (c) 2018
Alex Barney, MIT licence (`src/third_party/libatrac9/LICENSE`), vendored from
its `C/src` folder. ORION changes two lines of `utility.c`, marked "ORION:",
so a shift by 32 bits and a negative signed left shift (both undefined in C)
no longer happen. It is linked into the PS5 application.

## Icon decoding

Title icons (PNG) are decoded by [stb_image](https://github.com/nothings/stb)
v2.30 (public domain or MIT; see `src/third_party/stb/LICENSE`), vendored as
`src/third_party/stb/stb_image.inc` at the same commit and by the same script
as stb_vorbis. It is linked into the PS5 application.

## Shader distance functions

The fragment shader in `src/gfx/gl_batch.cpp` adapts the two-dimensional
signed-distance functions published by Inigo Quilez
(https://iquilezles.org/articles/distfunctions2d/), which he releases under
the MIT licence.

## OpenGL runtime

The PS5 build statically links the
[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) SDK
(GPL-3.0-or-later), which contains Mesa (MIT) and OpenGNM PSBC components under
their own licenses. The SDK is fetched or selected at build time and is not
stored in this repository's source; a copy of release 1.0.2 is kept as this
repository's `deps-ps5-opengl-1.0.2` release for the build to fetch, and its
license texts ship inside that archive.

## Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The application does not redistribute or dynamically load the
complete libc++ or libc++abi archives.

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

ORION links these ports statically (`PACBREW_PACKAGES=libcurl` in the
Makefile) to download box art over HTTPS:

| Library | Version | License |
| --- | --- | --- |
| [curl](https://curl.se/) (libcurl) | 8.18.0 | curl license (MIT/X derivative) |
| [OpenSSL](https://www.openssl.org/) (libssl, libcrypto) | 3.5.2 | Apache-2.0 |
| [libpsl](https://github.com/rockdaboot/libpsl) | 0.21.5 | MIT |
| [zstd](https://github.com/facebook/zstd) | 1.5.x | BSD-3-Clause (dual-licensed with GPL-2.0) |
| [zlib](https://zlib.net/) | 1.3.2 | zlib license |

`assets/certs/cacert.pem` is Mozilla's CA certificate list as Ubuntu's
`ca-certificates` package bundles it, used to verify the box art server. It is
distributed under the [Mozilla Public License 2.0](https://www.mozilla.org/MPL/2.0/).

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Presentation assets

`sce_sys/icon0.png`, `sce_sys/pic0.dds` (shown while the title is selected) and
`sce_sys/pic1.dds` (shown while it starts) are the project's own artwork,
supplied by its owner; `background-source.png` and
`launch-background-source.png` are the pictures the two backgrounds were made
from. `sce_sys/snd0.at9`, the music the console plays while the title is
selected, is the owner's own track, encoded with
[ps5-at9-converter](https://github.com/blackbearreloaded/ps5-at9-converter)
(57.6 s, 192 kb/s, -28 LUFS, whole-track loop).
They are drawn by ORION's own renderer
(`HUI_HOME_ART=<dir> tools/host-snapshots.sh`, see AGENTS.md). The
cover art inside the app is rendered at start-up from invented titles; no real
product's name, artwork or trademark is used.

No proprietary runtime module, encryption key, or game file is included.
