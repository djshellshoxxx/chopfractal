# Third-party notices

Binary builds of ChopFractal (VST3, CLAP) include or link the following. ChopFractal's own code is all rights
reserved (see `LICENSE`); the components below keep their own licenses.

| Component | Used for | License |
|---|---|---|
| [JUCE](https://juce.com) 8.0.15 | Plugin framework, GUI, audio file decoding | AGPLv3 or the JUCE commercial license. Distributing a binary that links JUCE under the AGPL requires offering its complete corresponding source; a closed-source release needs a JUCE commercial license. |
| VST3 SDK (bundled with JUCE) | VST3 plugin format | Steinberg VST3 license (the SDK is dual-licensed with GPLv3) |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | CLAP plugin format wrapper | MIT |
| [CLAP](https://github.com/free-audio/clap) headers | CLAP plugin format | MIT |

## Data

The rhythm tables of the break engine (`modules/break_engine/src/tables.cpp`) are derived statistics (per-step onset
probabilities and velocity means for kick, snare and ghost notes, per genre) computed from the
[Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) by Gillick, Roberts, Engel, Eck and Bamman
(2019), "Learning to Groove with Inverse Sequence Transformations", ICML. The dataset is licensed under
[Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/). Only aggregate
numbers are embedded; no recording or MIDI file from the dataset is included. The derivation is documented in
`docs/research/BREAK_ANALYSIS.md`.

Alpha builds are for internal testing only. Resolve the licensing route in `docs/LICENSING.md` before sharing
any binary outside the project.

## Fonts (SIL Open Font License 1.1)

The editor embeds Latin subsets of four Google Fonts families (static instances cut from the variable files in
`google/fonts`, files in `plugin/assets/fonts`). The full license texts sit next to the fonts as `OFL-*.txt`. None of the
families declares a Reserved Font Name.

| Family | Use | Copyright |
|---|---|---|
| Inter (Regular, SemiBold) | interface text | Copyright 2020 The Inter Project Authors (https://github.com/rsms/inter) |
| JetBrains Mono (Medium) | numeric readouts | Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono) |
| Big Shoulders Stencil Display (Black) | plugin title | Copyright 2019 The Big Shoulders Project Authors (https://github.com/xotypeco/big_shoulders) |
| Rubik Spray Paint | the `cdl` signature | Copyright 2020 The Rubik Filtered Project Authors (https://github.com/NaN-xyz/Rubik-Filtered) |

These fonts are licensed under the SIL Open Font License, Version 1.1 (http://scripts.sil.org/OFL). The fonts are not sold
on their own.

### SIL Open Font License 1.1 (full text, identical for all four families)

```
This Font Software is licensed under the SIL Open Font License, Version 1.1.
This license is copied below, and is also available with a FAQ at:
https://scripts.sil.org/OFL


-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded, 
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```
