FROM fedora:%releasever%

RUN dnf install -y rpm-build python3-dnf-plugins-core && dnf clean all
RUN dnf install -y --setopt=install_weak_deps=false obs-studio obs-studio-devel && dnf clean all

RUN useradd -s /bin/bash -m rpm
RUN echo >> /etc/sudoers
RUN echo "rpm ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER rpm
WORKDIR /home/rpm
