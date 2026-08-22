# Reference copy of the Homebrew cask. The submitted copy lives in
# Homebrew/homebrew-cask; see ../README.md.
#
# sha256 changes every release. `brew bump-cask-pr` fills it in.

cask "portal-launcher" do
  version "1.0.5"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"

  url "https://github.com/Portal-Launcher/PortalLauncher/releases/download/#{version}/PortalLauncher-macOS-#{version}.zip",
      verified: "github.com/Portal-Launcher/PortalLauncher/"
  name "Portal Launcher"
  desc "Minecraft launcher for playing modpacks with friends"
  homepage "https://github.com/Portal-Launcher/PortalLauncher"

  livecheck do
    url :url
    strategy :github_latest
  end

  app "Portal Launcher.app"

  zap trash: [
    "~/Library/Application Support/PrismLauncher",
    "~/Library/Preferences/io.github.portal_launcher.PortalLauncher.plist",
  ]

  caveats do
    <<~EOS
      Portal Launcher is not notarized, so macOS will refuse to open it the first
      time. Right click the app and choose Open, or remove the quarantine flag:
        xattr -dr com.apple.quarantine "/Applications/Portal Launcher.app"

      Portal keeps its data where Prism Launcher does, so an existing Prism
      install's instances and accounts are picked up automatically.
    EOS
  end
end
