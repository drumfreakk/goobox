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
  goobox_kip_version.patch
)
b2sums=(65a1934a42ae8526fa82d223aeea0ee5e7ed294af187dfd32aff91e028b71fcd477de519282d3d0c5191ff98e9a87ef2264d5c60b9b9857a1efcadbc497c839a
        67b88a651c83cf940e8cc3a08b2251df30cac12b1d50ca7451ad77ef2b7c22a8f72ffd3e476193683e2ccb86ca56a4174b2c6d973fd46de5b39829c6e372b7e2
)

prepare() {
  cd $pkgname

  # Set prgname to application ID
  patch -Np1 -i ../goobox_kip_version.patch
}

build() {
  arch-meson $pkgname build
  meson compile -C build
}

package() {
  meson install -C build --destdir "$pkgdir"
}
