# Building on Ubuntu Bionic (18.04) and supporting glibc 2.27. This allows
# the following distros (and newer versions) to run the resulting binaries:
# * Ubuntu Bionic (2.27)
# * Debian Buster (2.28)
# * CentOS 8 (2.28)
FROM ubuntu:bionic as builder

RUN apt-get update && \
    apt-get -y --no-install-recommends install build-essential fakeroot pbuilder aptitude git pkg-config openssh-client ca-certificates


RUN git clone --branch=v0.22.0 --depth=1 https://github.com/iovisor/bcc.git /root/bcc && \
    git -C /root/bcc submodule update --init --recursive


RUN cd /root/bcc && \
    /usr/lib/pbuilder/pbuilder-satisfydepends && \
    PARALLEL=$(nproc) ./scripts/build-deb.sh release

RUN cd /root && git clone https://github.com/libbpf/libbpf.git && \
    cd /root/libbpf && git checkout 030ff87857090ae5c9d74859042d05bfb3b613a2  && cd src && mkdir build root && BUILD_STATIC_ONLY=y OBJDIR=build DESTDIR=root make install

FROM ubuntu:bionic as compiler

RUN apt-get update && \
    apt-get install -y --no-install-recommends git build-essential pkg-config libelf-dev libz-dev libelf1 software-properties-common

RUN add-apt-repository ppa:longsleep/golang-backports && \
    apt-get install -y --no-install-recommends golang-1.17-go

ENV PATH="/usr/lib/go-1.17/bin:$PATH"

COPY --from=builder /root/bcc/libbcc_*.deb /tmp/libbcc.deb
COPY --from=builder /root/bcc/libbpf-tools  /tmp/libbpf-tools
COPY --from=builder /root/libbpf/ /tmp/libbpf


RUN cd /tmp/libbpf/src && make install
RUN dpkg -i /tmp/libbcc.deb

COPY ./ /go/ebpf_exporter

RUN cd /go/ebpf_exporter && \
    CC=gcc CGO_CFLAGS="-I /usr/include/bpf" CGO_LDFLAGS="-lbpf" \
    GOPROXY="off" GOFLAGS="-mod=vendor" go install -v -ldflags=" \
    -X github.com/prometheus/common/version.Version=$(git describe) \
    -X github.com/prometheus/common/version.Branch=$(git rev-parse --abbrev-ref HEAD) \
    -X github.com/prometheus/common/version.Revision=$(git rev-parse --short HEAD) \
    -X github.com/prometheus/common/version.BuildUser=docker@$(hostname) \
    -X github.com/prometheus/common/version.BuildDate=$(date --iso-8601=seconds) \
    " ./cmd/ebpf_exporter

RUN cd /go/ebpf_exporter/examples/CORE && make all
RUN /root/go/bin/ebpf_exporter --version

FROM ubuntu:bionic

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends build-essential pkg-config libelf-dev libz-dev software-properties-common

COPY --from=builder /root/bcc/libbcc_*.deb /tmp/libbcc.deb
COPY --from=builder /root/bcc/libbpf-tools  /tmp/libbpf-tools
COPY --from=builder /root/libbpf/ /tmp/libbpf
COPY --from=compiler /root/go/bin/ebpf_exporter /ebpf_exporter
COPY --from=compiler /go/ebpf_exporter/examples /examples

RUN cd /tmp/libbpf/src && make install
RUN dpkg -i /tmp/libbcc.deb
RUN find /tmp -name "*.git*"|xargs rm -Rf
RUN find /examples -name "*.git*"|xargs rm -Rf

ENV LD_LIBRARY_PATH=/lib64
