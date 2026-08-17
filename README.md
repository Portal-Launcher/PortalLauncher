<p align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="/program_info/org.prismlauncher.PrismLauncher.logo-darkmode.svg">
  <source media="(prefers-color-scheme: light)" srcset="/program_info/org.prismlauncher.PrismLauncher.logo.svg">
  <img alt="Portal Launcher" src="/program_info/portal-banner.png" width="55%">
</picture>
</p>

<p align="center">
  Portal Launcher is a custom launcher for Minecraft built around playing modpacks <b>with friends</b>: Modrinth's shared instances and friends list are built straight into the launcher, so everyone stays on the same version of the pack automatically.<br />
  <br />This is a <b>fork</b> of <a href="https://prismlauncher.org">Prism Launcher</a> and is <b>not</b> endorsed by it.
</p>

## Why does this exist?

Prism Launcher is a great launcher, but playing the same modpack with friends still means sending zips around, or everyone updating by hand and hoping the versions match. Modrinth built an official Shared Instances service for the Modrinth App; Portal brings that same service to a Prism-style launcher, along with a friends panel and the quality-of-life features that make shared packs actually pleasant to run. Packs shared from Portal work with the Modrinth App and vice versa, because it is the same service underneath.

## Differences from Prism Launcher

- **Modrinth Shared Instances, built in.** Share any instance from its Sharing page and push updates as you change it. Friends' copies sync automatically before they play: mods, configs, the pack icon, and Minecraft/loader version bumps. Hosts get an unpushed-changes warning, members see a changelog of what changed before they play, and pack owners can mark mods as optional so friends can turn those off without breaking sync.
- **Invites that feel modern.** Invite people with a `modrinth.com/share` link or directly by Modrinth username. Join from a link (File > Join Shared Pack, or paste it into Add Instance > Import), or accept pending invites right in the launcher.
- **A friends panel.** See which of your Modrinth friends are online and what they are playing, send and accept friend requests and pack invites, and join what a friend is playing in one click.
- **Discord Rich Presence.** People on Discord can see which pack you are playing while the game runs.
- **A real mod page when browsing.** Downloading mods shows galleries, download counts, versions, and changelogs for both Modrinth and CurseForge, not just a text blurb, plus a filter to hide versions that do not fit your instance.
- **Safer mod updates.** Updating mods snapshots them first: "Revert Last Mod Update" undoes a bad update in one click, and the update review warns about missing dependencies.
- **Modpack update notifications.** Instances installed from Modrinth or CurseForge get an update badge when the pack author publishes a new version, a right-click shortcut to update, and an opt-in prompt that offers the update right before you launch.
- **Your library, your order.** Drag instances around to arrange them exactly how you like, move packs between groups from the right-click menu, and filter everything with the search bar (Ctrl+F).
- **Small things that add up.** A Servers page with live ping, MOTD and player counts, a duplicate mod finder, a Play button that turns into Stop while the game runs, a resume button that jumps back into the instance you last played, CurseForge modpack import from share codes, already-downloaded mods reused across instances instead of redownloading, and shared/joined badges on instance cards.

Everything else works exactly like Prism Launcher, and it reads the same data folder, so your existing instances, accounts, and settings carry over as-is.

## Installation

- **Installer (recommended):** download `PortalLauncher-Setup-*.exe` from the [latest release](https://github.com/TinsleyDevers/PortalLauncher/releases/latest) and run it. It installs for the current user only (no admin prompt), adds Start Menu and desktop shortcuts, and can be removed from Add or Remove Programs.
- **Portable zip:** download `PortalLauncher-Windows-MSVC-*.zip`, unzip it anywhere, and run `prismlauncher.exe`. This is also what the in-app updater installs.
- Windows SmartScreen may warn because the build is not signed: click "More info", then "Run anyway". Every release ships a `.sha256` file next to each download if you want to verify it.
- If you already use Prism Launcher, Portal picks up your existing instances and accounts automatically.
- Sign in with your Modrinth account from the Sharing page of any instance to start sharing.

## Community & Support

Found a bug or have a suggestion? Open a [GitHub issue](https://github.com/TinsleyDevers/PortalLauncher/issues). Please do not ask the upstream Prism Launcher community for help with Portal: if something here is broken, it is this fork's fault, not theirs.

## Building

Portal builds the same way Prism Launcher does. Follow the upstream [build instructions](https://prismlauncher.org/wiki/development/build-instructions/), cloning this repository instead. The launcher lives on the `shared-instances` branch.

## License

The launcher code is licensed GPL-3.0-only, the same as the Prism Launcher code it forks. All upstream copyright notices are preserved. The Portal name, logo, and banner belong to this fork and are not covered by the upstream license.
