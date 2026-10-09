# Licensing

## Current state: all rights reserved, on purpose

The root [`LICENSE`](../LICENSE) grants nobody any rights except the copyright holder. That is the choice that
keeps the most options open. You can later move to open source, a commercial license, a dual license, or any mix,
because:

- the ten portable modules, the composition root and the tools contain **only code written for this project**
  plus the C++ standard library (no third-party code is mixed in);
- no outside contributions have been accepted, so there is a single copyright holder.

An open-source license, by contrast, cannot be taken back for copies already distributed, and a copyleft license
would bind the whole project. Starting proprietary costs nothing and can always be loosened.

## Changing it later

```sh
python3 tools/set_license.py MIT          # or Apache-2.0, LicenseRef-ChopFractal-Commercial, ...
# then replace the text of LICENSE
```

`LICENSE_ID` is the single identifier; every `module.json` and generated README follows it, and
`tools/check_modules.py` fails if they ever disagree.

## What you need to know before selling a plugin (not legal advice; verify current terms)

- **JUCE** (the plugin shell, `plugin/`) is offered under AGPLv3 or a paid commercial license. Distributing a
  closed-source plugin built on JUCE needs a JUCE commercial license; distributing under the AGPL would require
  publishing the plugin's source under the AGPL. Decide this before the first public build. The portable modules do
  not depend on JUCE and are unaffected.
- **VST3 SDK**: JUCE bundles Steinberg's VST3 SDK, which has its own terms (and a "VST" trademark/compatibility
  agreement for using the logo). Check Steinberg's current terms.
- **Contributions**: to keep the freedom to relicense, only merge outside contributions with a signed license grant
  or copyright assignment.
- **Modules as products**: because the modules are standalone and dependency-free, they can be licensed separately
  from the plugin (for example commercially to other developers) if you ever want to.
