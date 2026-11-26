FROM debian:bookworm-slim

# Debian supplies most static archives. Build the missing archives from its
# signed source repositories so that their headers and dependencies match.
RUN set -eux; \
    sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        build-essential ca-certificates dpkg-dev meson ninja-build pkg-config \
        libcairo2-dev libcrypt-dev libfribidi-dev libglib2.0-dev libgraphite2-dev \
        libpam0g-dev libpixman-1-dev libselinux1-dev libthai-dev \
        libx11-dev libxext-dev libxrandr-dev libxft-dev; \
    mkdir /tmp/static-deps; cd /tmp/static-deps; \
    apt-get source pango1.0 harfbuzz util-linux; \
    mkdir -p /usr/local/share/lok/sources /usr/local/share/lok/licenses; \
    cp *.dsc *.tar.* /usr/local/share/lok/sources/; \
    for source in pango1.0 harfbuzz util-linux; do \
        cp "$source"-*/debian/copyright "/usr/local/share/lok/licenses/$source"; \
    done; \
    cd /usr/local/share/lok/sources; \
    packages='libblkid-dev libbrotli-dev libc6-dev libcairo2-dev libcrypt-dev libdatrie-dev libexpat1-dev libffi-dev libfontconfig-dev libfreetype-dev libfribidi-dev libglib2.0-dev libpcre2-dev libpixman-1-dev libpng-dev libselinux1-dev libsepol-dev libstdc++-12-dev libthai-dev libx11-dev libxau-dev libxcb1-dev libxdmcp-dev libxext-dev libxrandr-dev libxrender-dev zlib1g-dev'; \
    dpkg-query -W -f='${source:Package}=${source:Version}\n' $packages > packages.txt; \
    for pkg in $packages; do \
        cp "/usr/share/doc/$pkg/copyright" "/usr/local/share/lok/licenses/$pkg"; \
    done; \
    apt-get source --download-only $(dpkg-query -W -f='${source:Package}=${source:Version} ' \
        libc6-dev libcairo2-dev libcrypt-dev libdatrie-dev libfribidi-dev libglib2.0-dev libsepol-dev libthai-dev); \
    cp /usr/share/common-licenses/LGPL-2.1 /usr/share/common-licenses/MPL-1.1 /usr/local/share/lok/licenses/; \
    rm -rf /var/lib/apt/lists/*

ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

WORKDIR /tmp/static-deps
# The release tarball already contains the generated autotools files.
RUN set -eux; \
    cd util-linux-*/; \
    ./configure --disable-all-programs --enable-libblkid --enable-libmount \
        --disable-shared --enable-static --without-python --without-ncurses; \
    make -j4 -o aclocal.m4 -o Makefile.in -o configure -o config.h.in libmount.la; \
    install -m 644 .libs/libmount.a /usr/local/lib/

RUN set -eux; \
    meson setup hb-build harfbuzz-*/ --prefix=/usr/local --libdir=lib \
        --buildtype=release --default-library=static --auto-features=disabled \
        -Dfreetype=enabled -Dglib=enabled -Dgobject=enabled \
        -Dtests=disabled; \
    ninja -C hb-build -j4 install; \
    meson setup pango-build pango1.0-*/ --prefix=/usr/local --libdir=lib \
        --buildtype=release --default-library=static --auto-features=disabled \
        -Dfontconfig=enabled -Dfreetype=enabled -Dcairo=enabled -Dlibthai=enabled \
        -Dintrospection=disabled; \
    ninja -C pango-build -j4 install; \
    cd /; rm -rf /tmp/static-deps

# Match the Fontconfig syntax used by current desktop distributions.
RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends curl gperf; \
    cd /tmp; \
    archive=fontconfig-2.18.4.tar.xz; \
    curl -fsSL --retry 3 "https://gitlab.freedesktop.org/api/v4/projects/890/packages/generic/fontconfig/2.18.4/$archive" -o "$archive"; \
    echo "95013637eff5f81d4871dd16cd098c273264c16d0f9f0b27cc43b81b2794bee3  $archive" | sha256sum -c; \
    tar -xf "$archive"; \
    cd fontconfig-2.18.4; \
    ./configure --prefix=/usr/local --sysconfdir=/etc --localstatedir=/var \
        --enable-static --disable-shared --disable-docs --disable-nls; \
    make -j4; \
    install -m 644 src/.libs/libfontconfig.a /usr/local/lib/; \
    install -m 644 fontconfig.pc /usr/local/lib/pkgconfig/; \
    cp COPYING /usr/local/share/lok/licenses/fontconfig; \
    cd /tmp; mv "$archive" /usr/local/share/lok/sources/; \
    rm -r fontconfig-2.18.4 /var/lib/apt/lists/*

WORKDIR /src
