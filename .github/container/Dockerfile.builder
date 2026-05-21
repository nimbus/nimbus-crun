ARG FEDORA_IMAGE=fedora:44
FROM ${FEDORA_IMAGE}

RUN dnf install -y \
      autoconf \
      automake \
      bash \
      binutils \
      coreutils \
      findutils \
      gcc \
      git \
      libcap-devel \
      libseccomp-devel \
      libtool \
      make \
      patchelf \
      patch \
      pkgconf-pkg-config \
      python3 \
      systemd-devel \
      yajl-devel \
  && dnf clean all \
  && rm -rf /var/cache/dnf
