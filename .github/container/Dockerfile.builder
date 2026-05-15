ARG FEDORA_IMAGE=fedora:44
FROM ${FEDORA_IMAGE}

RUN dnf install -y \
      autoconf \
      automake \
      bash \
      coreutils \
      findutils \
      gcc \
      git \
      libcap-devel \
      libkrun-devel \
      libseccomp-devel \
      libtool \
      make \
      patch \
      pkgconf-pkg-config \
      python3 \
      systemd-devel \
      yajl-devel \
  && dnf clean all \
  && rm -rf /var/cache/dnf
