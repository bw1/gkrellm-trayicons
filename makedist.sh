#!/bin/sh

# This script creates a distribution tarball of the plugin.

make clean
VERSION=`cat VERSION`
FILE="gkrellm-trayicons-$VERSION.tar.gz"
cd ..
cp -pr gkrellm-trayicons gkrellm-trayicons-$VERSION
tar -c gkrellm-trayicons-$VERSION | gzip -c > $FILE
rm -rf gkrellm-trayicons-$VERSION
