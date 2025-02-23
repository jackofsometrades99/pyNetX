FROM quay.io/pypa/manylinux2014_x86_64

# 1) Install system build dependencies
RUN yum install -y \
    gcc gcc-c++ make autoconf automake libtool \
    perl \
    wget tar \
    tinyxml2-devel \
    zlib-devel \
 && yum clean all \
 && rm -rf /var/cache/yum

# 2) Build & install modern OpenSSL (static)
ARG OPENSSL_VERSION=1.1.1w
WORKDIR /tmp
RUN wget https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz && \
    tar xf openssl-${OPENSSL_VERSION}.tar.gz && \
    cd openssl-${OPENSSL_VERSION} && \
    ./config no-shared -fPIC \
        --prefix=/usr/local/openssl-${OPENSSL_VERSION} \
        --libdir=lib \
        --openssldir=/usr/local/openssl-${OPENSSL_VERSION} \
    && make -j8 \
    && make install

ENV OPENSSL_ROOT_DIR=/usr/local/openssl-${OPENSSL_VERSION}
ENV PATH="${OPENSSL_ROOT_DIR}/bin:${PATH}"

# 3) Build & install libssh2 (static), linking to new OpenSSL
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
      --with-libssl-prefix=/usr/local/openssl-1.1.1w \
      --prefix=/usr/local/libssh2-1.11.1 \
      CFLAGS="-fPIC -I/usr/local/openssl-1.1.1w/include" \
      LDFLAGS="-L/usr/local/openssl-1.1.1w/lib -lpthread -ldl"\
    && make -j8 \
    && make install

ENV PKG_CONFIG_PATH="/usr/local/libssh2-1.11.1/lib/pkgconfig:${PKG_CONFIG_PATH}"

ENV LIBSSH2_ROOT=/usr/local/libssh2-${LIBSSH2_VERSION}
# If you really want .pc usage, or partial dynamic, you'd do: 
# ENV PKG_CONFIG_PATH="${LIBSSH2_ROOT}/lib/pkgconfig:${PKG_CONFIG_PATH}"

# 4) Install Python build tools (example: Python 3.11)
RUN /opt/python/cp311-cp311/bin/python -m pip install --upgrade pip
RUN /opt/python/cp311-cp311/bin/python -m pip install \
    setuptools wheel cmake scikit-build pybind11 auditwheel twine

# 5) Optionally set pybind11 + CMAKE_PREFIX_PATH for your code
ENV pybind11_DIR="/opt/python/cp311-cp311/lib/python3.11/site-packages/pybind11/share/cmake/pybind11"
ENV CMAKE_PREFIX_PATH="${pybind11_DIR}:${CMAKE_PREFIX_PATH}"

# 6) Copy in your code & build
WORKDIR /io
COPY . .

