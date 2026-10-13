# TinySoundFont

`tsf.h` is TinySoundFont by Bernhard Schelling (based on SFZero by Steve
Folta), a single-header SoundFont2 synthesizer. MIT licence; the full text is
at the top of the header.

- Source: https://github.com/schellingb/TinySoundFont
- Vendored from commit `790a219810cb0fca5defa8cdbd88e2487e5efc7a` (2025-06-05)
- sha256 `70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197`

Used by `Source/Engine/SoundFont.cpp` (the only file that defines
`TSF_IMPLEMENTATION`) to play the preview's notes through GeneralUser GS.
