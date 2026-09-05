# Mod system

ReXGlue discovers manually installed native mod packages under `mods/` beside
the executable. Set `mods_data_root` to use another root. Discovery reads
package metadata and payload paths without loading native code or changing the
filesystem. F1 displays every direct-child package, including invalid records,
and reports its compatibility and active state.

## Package layout

```text
mods/<id>/
  mod.toml
  icon.png                         optional
  code/<runtime-platform>/<binary>
```

The folder name is the manifest package ID. IDs contain one to 63 lowercase
ASCII letters or digits separated by single hyphens. A folder and manifest ID
must match exactly. Discovery scans one level only; linked package folders are
not traversed, and linked manifest, platform, or binary paths are not accepted
as package payloads.

Recognized runtime platforms are `windows-x64`, `windows-arm64`, `linux-x64`,
`linux-arm64`, `macos-x64`, and `macos-arm64`. The binary name uses the native
loader convention: Windows uses `<code><config-postfix>.dll`, Linux uses
`lib<code><config-postfix>.so`, and macOS uses
`lib<code><config-postfix>.dylib`. Native code is loaded only from the
platform-qualified directory; a flat `code/` binary is not a package payload.

## Version-1 manifest

```toml
manifest_version = 1

[mod]
id = "state-inspector"
name = "State Inspector"
version = "1.0"
author = "Aeshur"
description = "Displays title state for development."
min_game_version = "1.0.0"
code = "state_inspector"
plugin_abi = 1
```

`manifest_version`, `[mod].id`, `name`, `version`, `code`, and `plugin_abi` are
required. Package versions use exactly two non-negative numeric components
(`major.minor`). `min_game_version` is optional, uses exactly three non-negative
numeric components (`major.minor.patch`), and requires the host game version to
meet the stated minimum. `code` is one filename stem without a path or
extension. Every valid package must provide a matching native binary for the
current runtime platform and declare the current plugin ABI.

`author` and `description` are optional display metadata. Unknown top-level or
`[mod]` fields produce visible nonblocking warnings. A malformed manifest,
identity mismatch, version mismatch, missing current-platform binary, or plugin
ABI mismatch remains visible as an invalid or incompatible package and blocks
that package from the enabled set.

## Profile loadout

The active profile owns `mod_order.txt`. Each nonempty line is one exact package
ID in native load order. Whitespace is trimmed, blank lines and lines beginning
with `#` are ignored, and comments are not retained when F1 writes the file.
An absent file means an empty loadout and does not create a file at startup.

Malformed IDs, repeated IDs, missing packages, and selected packages with
blocking catalog diagnostics fail closed for the complete desired set. The game
still starts and F1 shows every line diagnostic so the file can be repaired.
F1 stages enable, disable, and reorder changes; Apply writes one ID per line
atomically, writes a zero-byte file for an explicit empty selection, and leaves
the running native set unchanged until restart. Replacing an invalid current
file requires explicit confirmation.

Rescanning refreshes installed package facts while preserving the staged IDs and
their order. Closing F1 with unapplied changes requires explicit discard
confirmation. The manager does not install, import, update, replace, delete, or
reload packages.

Native plugins retain the `rex_mod_create`, `rex_mod_abi_version`,
`OnCreateDialogs`, `OnModuleLaunched`, and `OnShutdown` ABI and lifecycle.
