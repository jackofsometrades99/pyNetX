FROM quay.io/pypa/manylinux2014_x86_64

RUN yum install -y \
    gcc gcc-c++ make autoconf automake libtool \
    perl wget tar tinyxml2-devel zlib-devel \
 && yum clean all \
 && rm -rf /var/cache/yum

ARG OPENSSL_VERSION=1.1.1w
WORKDIR /tmp
RUN wget https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz && \
    tar xf openssl-${OPENSSL_VERSION}.tar.gz && \
    cd openssl-${OPENSSL_VERSION} && \
    ./config no-shared -fPIC \
      --prefix=/usr/local/openssl-${OPENSSL_VERSION} \
      --libdir=lib \
      --openssldir=/usr/local/openssl-${OPENSSL_VERSION} && \
    make -j8 && make install

ENV OPENSSL_ROOT_DIR=/usr/local/openssl-${OPENSSL_VERSION}
ENV PATH="${OPENSSL_ROOT_DIR}/bin:${PATH}"

ARG LIBSSH2_VERSION=1.11.1
WORKDIR /tmp
RUN wget https://www.libssh2.org/download/libssh2-${LIBSSH2_VERSION}.tar.gz && \
    tar xf libssh2-${LIBSSH2_VERSION}.tar.gz && \
    cd libssh2-${LIBSSH2_VERSION} && \
    ./configure \
      --enable-static \
      --disable-shared \
      --disable-examples-build \
      --with-libz \
      --with-libssl-prefix=/usr/local/openssl-${OPENSSL_VERSION} \
      --prefix=/usr/local/libssh2-${LIBSSH2_VERSION} \
      CFLAGS="-fPIC -I/usr/local/openssl-${OPENSSL_VERSION}/include" \
      LDFLAGS="-L/usr/local/openssl-${OPENSSL_VERSION}/lib -lpthread -ldl" && \
    make -j8 && make install

ENV PKG_CONFIG_PATH="/usr/local/libssh2-${LIBSSH2_VERSION}/lib/pkgconfig:${PKG_CONFIG_PATH}"

WORKDIR /io
COPY . .

CMD ["/bin/bash"]