<p align="center">
  <img alt="Portal Launcher" src="/program_info/PortalLauncherBanner2.png" width="55%">
</p>

<h1 align="center">Portal Launcher</h1>

<p align="center">
  Portal Launcher is an <b>open-source Minecraft launcher</b> for sharing and automatically syncing modpacks <b>with friends</b>.<br />
  Built on <a href="https://prismlauncher.org"><b>Prism Launcher</b></a>, Portal adds <b>Modrinth Shared Instances</b>, automatic modpack syncing, a friends list, CurseForge and Modrinth browsing, safer mod updates, and multiplayer-focused tools.
  <br /><br />
  Share a Minecraft modpack once, update it whenever you want, and Portal keeps everyone on the same <b>mods, configs, Minecraft version, and mod loader</b> automatically.
  <br /><br />
  Portal Launcher is a <b>fork</b> of Prism Launcher and is <b>not endorsed by or affiliated with</b> the Prism Launcher project.
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
- **Bring your instances with you.** Add Instance > Other Launchers finds what you already have in the Minecraft Launcher, CurseForge, the Modrinth App, MultiMC, PolyMC, Prism Launcher, GDLauncher, ATLauncher and XMCL, and imports it with mods, worlds and settings. The other launcher keeps its copy.
- **Each mod stored once, however many instances use it.** Installing a mod a second instance already has links the same file instead of downloading or copying it, and Help > Reclaim Disk Space does the same for everything you already have: it tells you how much it would free before it touches anything.
- **Mod groups.** File the mods of a big pack under names like Performance or Optional from the Mods page, then filter the list by group.
- **Your library, your order.** Drag instances around to arrange them exactly how you like, move packs between groups from the right-click menu, and filter everything with the search bar (Ctrl+F).
- **Small things that add up.** A Servers page with live ping, MOTD and player counts, a duplicate mod finder, a Play button that turns into Stop while the game runs, a resume button that jumps back into the instance you last played, CurseForge modpack import from share codes, already-downloaded mods reused across instances instead of redownloading, and shared/joined badges on instance cards.

Everything else works exactly like Prism Launcher, and it reads the same data folder, so your existing instances, accounts, and settings carry over as-is.

## Installation

- **Installer (recommended):** download `PortalLauncher-Setup-*.exe` from the [latest release](https://github.com/Portal-Launcher/PortalLauncher/releases/latest) and run it. It installs for the current user only (no admin prompt), adds Start Menu and desktop shortcuts, and can be removed from Add or Remove Programs.
- **Portable zip:** download `PortalLauncher-Windows-MSVC-*.zip`, unzip it anywhere, and run `prismlauncher.exe`. This is also what the in-app updater installs.
- Windows SmartScreen may warn because the build is not code signed yet: click "More info", then "Run anyway". If you would rather check the download than trust it, see [Security and privacy](#security-and-privacy) above: every file is built in public and can be verified against this repository.
- If you already use Prism Launcher, Portal picks up your existing instances and accounts automatically.
- Sign in with your Modrinth account from the Sharing page of any instance to start sharing.

## Community & Support

Found a bug or have a suggestion? Open a [GitHub issue](https://github.com/Portal-Launcher/PortalLauncher/issues). Please do not ask the upstream Prism Launcher community for help with Portal: if something here is broken, it is this fork's fault, not theirs.

## Building

Portal builds the same way Prism Launcher does. Follow the upstream [build instructions](https://prismlauncher.org/wiki/development/build-instructions/), cloning this repository instead. The launcher lives on the `main` branch.

## Security and privacy

Portal handles your Minecraft account and your Modrinth account, so here is exactly what it does with them.

- **Portal never sees your Microsoft password.** Signing in to Minecraft uses Microsoft's official device code flow, the same one Prism Launcher uses: the launcher shows you a short code, you type it on Microsoft's own sign-in page in your browser, and Microsoft hands the launcher a token. Your password is never typed into Portal and never passes through it.
- **Modrinth sign-in happens in your browser.** Portal opens Modrinth's own sign-in page and receives a session token back. The token is stored in `prismlauncher.cfg` in your launcher data folder, the same file and the same way the Modrinth App and Prism store theirs, and it is only ever sent to `*.modrinth.com`. Sign out any time from the Sharing page of any instance.
- **No telemetry, no analytics, no ads.** Portal does not phone home and there is nothing to opt out of. It talks to Microsoft and Mojang to sign you in and download the game, to Modrinth and CurseForge when you browse mods or sync a shared pack, to GitHub to check for launcher updates, and to Prism Launcher's metadata server for Minecraft version data. That is the entire list.
- **The binaries here are built from this source, in public.** From 1.0.6 onwards every release is built by GitHub Actions from a specific commit and carries a signed build provenance attestation, so you do not have to take my word for what is in the download. With the [GitHub CLI](https://cli.github.com):

  ```
  gh attestation verify PortalLauncher-Setup-1.0.6.exe --repo Portal-Launcher/PortalLauncher
  ```

  Releases up to 1.0.5 were built on my own machine and have no attestation. Every release, old or new, ships a `.sha256` beside each file if you only want to confirm the download is intact.

## License

The launcher code is licensed GPL-3.0-only, the same as the Prism Launcher code it forks. All upstream copyright notices are preserved. The Portal name, logo, and banner belong to this fork and are not covered by the upstream license.
