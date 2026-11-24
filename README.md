# Better Link's Basement

Removes the chest in Link's basement and replaces it with:

- the lantern-oil pot
- the red-potion pot
- a lit torch for visibility

Built for [Dusklight](https://github.com/TwilitRealm/dusklight) using its
[modding SDK](https://github.com/TwilitRealm/dusklight/blob/main/docs/modding.md).

## Build

Use the Dusklight template's standard CMake build:

```sh
cmake -B build
cmake --build build
```

The resulting `build/mods/better_links_basement.dusk` is for the local platform. The included GitHub Actions workflow
builds the supported platform bundles and combines them into a distributable `.dusk`.

## Install

Copy the bundle into the game's mods folder:

- Windows: `%APPDATA%\TwilitRealm\Dusklight\mods`
- Linux: `~/.local/share/TwilitRealm/Dusklight/mods`
- macOS: `~/Library/Application Support/TwilitRealm/Dusklight/mods`

During development, click **Reload** in the in-game mod manager after replacing the bundle.
