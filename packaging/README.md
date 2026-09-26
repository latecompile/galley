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

1. **A public repository.** `url` and `source` in `PKGBUILD` point at
   `https://github.com/latecompile/galley`. Until that exists and has been
   pushed, the PKGBUILD only builds from a local path.
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

`PKGBUILD.release` builds `galley` from a tagged tarball. Use it once a
version is tagged: set `pkgver`, run `updpkgsums` to fill in the checksum, and
push it to a separate `galley` AUR repository. The two can coexist — one for
people who want releases, one for people who want the tip.
