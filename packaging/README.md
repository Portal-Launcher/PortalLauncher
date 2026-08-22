# Packaging Portal Launcher

Reference copies of the package manager manifests, and what to do with each of
them. None of these files are read by a build: every package manager keeps its
own repository, and this directory exists so the definitions live with the
source, are reviewed with it, and are easy to copy when a release goes out.

Being installable with `winget install PortalLauncher` reads as legitimate
software in a way that "download this exe from a GitHub account you have never
heard of" never will, which is the whole point of doing any of this.

Order to do them in: **winget, Scoop, AUR, Homebrew, Flathub.** The first two
cover almost everyone who will install Portal; the rest are reputational.

Everything below assumes a release already exists, built by CI, with the assets
named the way `.github/workflows/release.yml` names them.

---

## winget

Package identifier: `PortalLauncher.PortalLauncher`

**First submission, once:**

```
winget install Microsoft.WingetCreate
wingetcreate new https://github.com/Portal-Launcher/PortalLauncher/releases/download/1.0.5/PortalLauncher-Setup-1.0.5.exe
```

`wingetcreate` downloads the installer, works out the hashes, prompts through
the metadata and opens the pull request against
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) for you. The
three files in `winget/` are what the answers should produce, so fill the
prompts to match them.

Things reviewers will look at, and the answers:

- The installer is per user (`RequestExecutionLevel user` in the NSIS script),
  so `Scope: user` and no administrator prompt. Say so, it makes review easier.
- The uninstall registry key is still `PrismLauncher`, because the binary and
  data folder keep that name so existing Prism installs keep working. That is
  what `AppsAndFeaturesEntries.ProductCode` has to say, or `winget upgrade`
  will not match the installed version.
- The publisher does not match the package name, and the binary is unsigned.
  Neither is disqualifying, but expect a question. "Fork of Prism Launcher,
  built in public by GitHub Actions with a provenance attestation" is a good
  answer, and links to the release page prove it.

**Every release after that** is automatic: `.github/workflows/publish.yml`
opens the update pull request when a release is published. It needs one secret
on this repository, `WINGET_TOKEN`, a classic personal access token with
`public_repo` scope on an account that has forked winget-pkgs. Until that
secret exists the job simply fails, and nothing else is affected.

## Scoop

Fits Portal well, since the release zip is already a self-contained folder.

Submit `scoop/portal-launcher.json` to the **extras** bucket by opening a pull
request against [ScoopInstaller/Extras](https://github.com/ScoopInstaller/Extras)
with the file placed at `bucket/portal-launcher.json`. An official bucket
carries more trust than a personal one, so start there rather than telling
people to add a Portal bucket.

Fill in `version` and the `hash` from the release before submitting. After
that Scoop's own automation keeps it current: `checkver` watches the GitHub
releases and `autoupdate` reads the hash straight out of the `.sha256` file
CI publishes next to each asset.

## AUR

Cheapest listing here, and Arch users find things through the AUR.

```
git clone ssh://aur@aur.archlinux.org/portal-launcher-bin.git
cp packaging/aur/PKGBUILD portal-launcher-bin/
cd portal-launcher-bin
# fill in the real sha256sums from the .sha256 assets on the release
makepkg --printsrcinfo > .SRCINFO
makepkg -si   # build and install it once to check it actually works
git add PKGBUILD .SRCINFO && git commit -m "portal-launcher-bin 1.0.5" && git push
```

Needs an AUR account with an SSH key uploaded. Bumping a release is the same
three steps: change `pkgver`, refresh the sums, regenerate `.SRCINFO`, push.

The PKGBUILD packages the AppImage and installs everything under
`portal-launcher` names, so it cannot collide with the `prismlauncher` package
even though the executable inside is still called `prismlauncher`.

**Blocked until** a release actually has Linux assets, which means the CI build
has to have gone green on Linux at least once.

## Homebrew Cask

`brew install --cask portal-launcher`. Pull request `homebrew/portal-launcher.rb`
into [Homebrew/homebrew-cask](https://github.com/Homebrew/homebrew-cask) at
`Casks/p/portal-launcher.rb`, with the real `sha256` filled in.

Unsigned apps are accepted; the cask already documents the Gatekeeper
workaround the same way the README documents SmartScreen. Later releases can
be bumped with `brew bump-cask-pr --version=1.0.6 portal-launcher`.

**Blocked until** the macOS build has gone green in CI and a release carries
`PortalLauncher-macOS-*.zip`.

## Flathub

The most work of the five, and the one with a real prerequisite.

**The app id is Portal's own now.** It is `io.github.portal_launcher.PortalLauncher`
(`program_info/CMakeLists.txt`), with the `program_info/` desktop, metainfo,
mime, icon and svg files named to match, and the AppImage step of
`.github/actions/package/linux/action.yml` updated. It used to be
`org.prismlauncher.PrismLauncher`, which Flathub would have rejected (a domain
we do not control) and which would have collided with Prism's own entry.

Things worth knowing about that id:

- `Launcher_AppID` reaches exactly one place in the code,
  `BuildConfig.LAUNCHER_APPID`, which is used for `setDesktopFileName` and for
  the flatpak shortcut command. Both are Linux and macOS only.
- It does **not** touch the data folder. That is `Launcher_CommonName`
  (`PrismLauncher`), which is what makes Portal read an existing Prism install,
  and it stays exactly as it is.
- Windows is completely unaffected. Nothing on Windows reads the app id.
- The underscore is not a typo: Flathub derives the id from the GitHub account
  that owns the Pages site, and normalises the hyphen in `Portal-Launcher` to
  an underscore. Worth confirming with the reviewers on the submission PR
  rather than guessing, since the id cannot be changed afterwards.

Once that is done, `flathub/io.github.portal_launcher.PortalLauncher.yml` is a
starting point, not a working manifest. The realistic path is to fork
[flathub/org.prismlauncher.PrismLauncher](https://github.com/flathub/org.prismlauncher.PrismLauncher),
swap the id, source and metadata for Portal's, keep their module files (cmark,
tomlplusplus, libdecor, glfw with its Wayland patches, gamemode), build it with
`flatpak-builder` on a Linux machine until it runs, and only then open the
submission pull request at
[flathub/flathub](https://github.com/flathub/flathub). Their manifest exists
because this program needs all of that to work in a sandbox, and rebuilding
that knowledge from scratch would be wasted effort.

## nixpkgs

`flake.nix`, `default.nix` and `nix/` are still upstream Prism's, and
`.github/workflows/nix.yml` no longer runs on its own for that reason. Worth
picking up after Flathub, not before.

---

## When cutting a release

1. CI builds it, attaches the assets and their `.sha256` files, and opens the
   release as a draft. Publish it.
2. winget updates itself, if `WINGET_TOKEN` is set.
3. Scoop updates itself.
4. AUR, Homebrew and Flathub need a version bump pushed by hand. Each is a
   two minute job once the package exists.
