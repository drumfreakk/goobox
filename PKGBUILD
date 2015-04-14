# Maintainer: Balló György <ballogyor+arch at gmail dot com>

pkgname=goobox
pkgver=3.4.0
pkgrel=1
pkgdesc="CD player and ripper for GNOME"
arch=('i686' 'x86_64')
url="http://people.gnome.org/~paobac/goobox/"
license=('GPL')
depends=('gst-plugins-base' 'brasero' 'libmusicbrainz5' 'libdiscid' 'libcoverart' 'libnotify')
makedepends=('intltool' 'itstool')
optdepends=('gst-plugins-good: rip CDs into flac and wav formats')
install=$pkgname.install
source=(http://ftp.gnome.org/pub/GNOME/sources/$pkgname/${pkgver%.*}/$pkgname-$pkgver.tar.xz)
sha256sums=('b41f414601e44af5dbe931f3bc81d2acf984ecd357a8f92861437629e9790552')

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
