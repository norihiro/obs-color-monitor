FROM fedora:%releasever%

RUN dnf install -y rpm-build python3-dnf-plugins-core && dnf clean all
RUN dnf install -y obs-studio obs-studio-devel && dnf clean all
RUN dnf install -y qt5-qtbase-devel qt5-qtbase-private-devel && dnf clean all

RUN useradd -s /bin/bash -m rpm
RUN echo >> /etc/sudoers
RUN echo "rpm ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER rpm
WORKDIR /home/rpm
