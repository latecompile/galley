# Packaging

## Building the package locally

```sh
cd packaging
makepkg -f          # builds galley-git from the repository tip
sudo pacman -U galley-git-*.pkg.tar.zst
```

`makepkg` needs `base-devel`; the build also needs `tomlplusplus`, which is a
header-only makedepend and is not required at runtime.

## Publishing to the AUR

Three things have to exist first, and none of them can be done from here:

1. ~~**A public repository.**~~ Done — `url` and `source` point at
   `https://github.com/latecompile/galley`, which is live and tagged.
2. **An AUR account** with an SSH public key uploaded, and a matching entry
   in `~/.ssh/config`:

   ```
   Host aur.archlinux.org
     User aur
     IdentityFile ~/.ssh/<your key>
   ```
3. **`namcap`**, to lint the PKGBUILD and the built package before pushing.

Then:

```sh
git clone ssh://aur@aur.archlinux.org/galley-git.git aur-galley-git
cd aur-galley-git
cp ../packaging/PKGBUILD .
makepkg --printsrcinfo > .SRCINFO      # the AUR reads this, not the PKGBUILD
git add PKGBUILD .SRCINFO
git commit -m "Initial import"
git push
```

`.SRCINFO` must be regenerated and committed on every change, or the AUR will
show stale metadata.

## Two PKGBUILDs

`PKGBUILD` builds `galley-git` from the repository tip. Its `pkgver()` reports
`0.0.0.r<commits>.g<sha>` until the first tag and `<tag>.r<n>.g<sha>` after,
so it keeps sorting correctly once releases start.

`PKGBUILD.release` builds `galley` from a tagged tarball. v0.1.0 is tagged,
`pkgver` is set to it, and the checksum has been filled in with `updpkgsums`
against the published tarball — so it is ready to push to a separate `galley`
AUR repository. Re-run `updpkgsums` on every version bump. The two can coexist
— one for people who want releases, one for people who want the tip.
