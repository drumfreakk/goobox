# Maintainer: Balló György <ballogyor+arch at gmail dot com>

pkgname=goobox
pkgver=3.0.1
pkgrel=1
pkgdesc="CD player and ripper for GNOME"
arch=('i686' 'x86_64')
url="http://people.gnome.org/~paobac/goobox/"
license=('GPL')
depends=('brasero' 'libmusicbrainz3' 'libnotify' 'xdg-utils')
makedepends=('gconf' 'intltool' 'gnome-doc-utils')
optdepends=('gstreamer0.10-good-plugins: rip CDs into flac and wav format')
install=$pkgname.install
source=(http://ftp.gnome.org/pub/GNOME/sources/$pkgname/${pkgver%.*}/$pkgname-$pkgver.tar.xz
        fix-notifications.patch)
sha256sums=('344351ab8a9aee9e1c7f490e84c972a0df57eec5b44d31247c7ef268bf4cb60e'
            '41f1307c271ee003d2e7435d6e9a7c5f72d3668ce0cc50842769c34f97c7fe99')

build() {
  cd "$srcdir/$pkgname-$pkgver"

  # https://bugzilla.gnome.org/show_bug.cgi?id=674121
  patch -Np1 -i "$srcdir/fix-notifications.patch"

  ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
              --disable-static --disable-scrollkeeper --disable-schemas-compile
  make
}

package() {
  cd "$srcdir/$pkgname-$pkgver"

  make DESTDIR="$pkgdir" install
}
