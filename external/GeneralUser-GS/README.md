# GeneralUser GS

The preview plays the transcribed notes through **GeneralUser GS 2.0.3**, a
General MIDI SoundFont by S. Christian Collins (https://www.schristiancollins.com).
Licence: `LICENSE.txt` here (GeneralUser GS License v2.0: free to use in
software, private or commercial).

The bank itself (32 MB) is not in this repository. It lives, shared by every
AudioFlower plugin, at

    ~/Library/Application Support/AudioFlower/soundfonts/GeneralUser-GS.sf2

`build.sh` fetches it there when missing (the installer will put it there for
users), pinned to a commit and checked against its digest:

- https://github.com/mrbumpy409/GeneralUser-GS/raw/97049183643d5fc5a9322a69c5b09efb667c6c3a/GeneralUser-GS.sf2
- sha256 `9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe`

Chosen 2026-10-13 by ear over MuseScore General (MIT, 216 MB): close in sound
and a seventh of the memory. Please credit it in the plugin's about / release
notes; its licence asks that the bank be hosted by us rather than deep-linked.
