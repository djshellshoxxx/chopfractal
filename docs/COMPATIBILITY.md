# Host compatibility (manual matrix)

Fill one row per host and OS before a public release. Automated coverage: Steinberg validator (Linux CI), headless shell tests (Linux, Windows, macOS).

| Host | Version | macOS arm64 / Intel | Windows 11 x64 | Notes |
|---|---|---|---|---|
| REAPER | 7.x | untested | untested | |
| Ableton Live | 12 | untested | untested | |
| FL Studio | 2024+ | untested | untested | |
| Cubase | 13/14 | untested | untested | validator-strict host |

Check per host: loads and rescans, 9 output buses (Main + Out 2-9) and MIDI input appear, state saves and reloads, drag-out of audio and MIDI, resize and interface scale, capture arms on transport start.
