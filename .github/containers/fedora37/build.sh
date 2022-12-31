#! /bin/bash
set -ex
.github/containers/fedora-common/build.sh obs-plugin-build/fedora37 fedora37-rpmbuild
echo 'FILE_NAME=fedora37-rpmbuild/*RPMS/**/*.rpm' >> $GITHUB_ENV
