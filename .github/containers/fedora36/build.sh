#! /bin/bash
set -ex
.github/containers/fedora-common/build.sh obs-plugin-build/fedora36 fedora36-rpmbuild
echo 'FILE_NAME=fedora36-rpmbuild/*RPMS/**/*.rpm' >> $GITHUB_ENV
