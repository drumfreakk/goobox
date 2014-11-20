# Maintainer: Balló György <ballogyor+arch at gmail dot com>

pkgname=goobox
pkgver=3.2.1
pkgrel=2
pkgdesc="CD player and ripper for GNOME"
arch=('i686' 'x86_64')
url="http://people.gnome.org/~paobac/goobox/"
license=('GPL')
depends=('gst-plugins-base' 'brasero' 'libmusicbrainz5' 'libdiscid' 'libcoverart' 'libnotify')
makedepends=('intltool' 'itstool')
optdepends=('gst-plugins-good: rip CDs into flac and wav formats')
install=$pkgname.install
source=(http://ftp.gnome.org/pub/GNOME/sources/$pkgname/${pkgver%.*}/$pkgname-$pkgver.tar.xz)
sha256sums=('c2dba51881902cad3593fc3b3dafd3c12038173a27c623350f1771a1bfbf09a7')

prepare() {
  cd "$srcdir/$pkgname-$pkgver"

  # Update libcoverart version number
  sed -i 's/LIBCOVERART_REQUIRED=1.0.0beta1/LIBCOVERART_REQUIRED=1.0.0/' configure{,.ac}
}

build() {
  cd "$srcdir/$pkgname-$pkgver"
  ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
              --disable-static --disable-schemas-compile
  make
}

package() {
  cd "$srcdir/$pkgname-$pkgver"
  make DESTDIR="$pkgdir" install
}
