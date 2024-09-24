# Maintainer: Balló György <ballogyor+arch at gmail dot com>

pkgname=goobox
pkgver=3.6.0
pkgrel=4
pkgdesc='CD player and ripper for GNOME'
arch=(x86_64)
url='https://gitlab.gnome.org/Archive/goobox'
license=(GPL-2.0-or-later)
depends=(
  brasero
  dconf
  gdk-pixbuf2
  glib2
  glibc
  gst-plugins-base
  gstreamer
  gtk3
  hicolor-icon-theme
  libcoverart
  libdiscid
  libmusicbrainz5
  pango
)
makedepends=(
  git
  glib2-devel
  itstool
  meson
)
optdepends=('gst-plugins-good: Rip CDs into FLAC, WAV or MP3 formats')
source=(
  "git+https://gitlab.gnome.org/Archive/goobox.git#tag=$pkgver"
  goobox-set-prgname.patch
)
b2sums=(
  65a1934a42ae8526fa82d223aeea0ee5e7ed294af187dfd32aff91e028b71fcd477de519282d3d0c5191ff98e9a87ef2264d5c60b9b9857a1efcadbc497c839a
  444be0fd47205d9291784f38685644b9d10b1cf1ffc295b644b98542bea84521c05d6e20b1b84dca50a953db8aa72d227ff5554e056821ba5a263eb99b3614a5
)

prepare() {
  cd $pkgname

  # Set prgname to application ID
  patch -Np1 -i ../goobox-set-prgname.patch
}

build() {
  arch-meson $pkgname build
  meson compile -C build
}

package() {
  meson install -C build --destdir "$pkgdir"
}
