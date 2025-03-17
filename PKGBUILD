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
b2sums=('65a1934a42ae8526fa82d223aeea0ee5e7ed294af187dfd32aff91e028b71fcd477de519282d3d0c5191ff98e9a87ef2264d5c60b9b9857a1efcadbc497c839a'
        '133dcfbc98254965655dcc16b1c41a8f99d4e87a26823495743030556c01ec8748607c3f47ea1ca9d0567ffcf1872c5f8f701622c44166cff87e92e100d0d694')

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
