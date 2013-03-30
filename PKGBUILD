# Maintainer: Balló György <ballogyor+arch at gmail dot com>

pkgname=goobox
pkgver=3.2.0
pkgrel=1
pkgdesc="CD player and ripper for GNOME"
arch=('i686' 'x86_64')
url="http://people.gnome.org/~paobac/goobox/"
license=('GPL')
depends=('gst-plugins-base' 'brasero' 'libmusicbrainz5' 'libdiscid' 'libcoverart' 'libnotify' 'xdg-utils')
makedepends=('intltool' 'itstool')
optdepends=('gst-plugins-good: rip CDs into flac and wav formats')
install=$pkgname.install
source=(http://ftp.gnome.org/pub/GNOME/sources/$pkgname/${pkgver%.*}/$pkgname-$pkgver.tar.xz)
sha256sums=('0bd5c31d53635588f46ad4d254866ff74356caee5edb45c1a3bce9c754cc6cb6')

build() {
  cd "$srcdir/$pkgname-$pkgver"

  sed -i 's/LIBCOVERART_REQUIRED=1.0.0beta1/LIBCOVERART_REQUIRED=1.0.0/' configure{,.ac}

  ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
              --disable-static --disable-schemas-compile
  make
}

package() {
  cd "$srcdir/$pkgname-$pkgver"

  make DESTDIR="$pkgdir" install
}
