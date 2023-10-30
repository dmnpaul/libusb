#!/bin/bash
# Builds Release and Debug variants for "all" scheme and packs the result as libusb-macos-dylib.7z
# Author: Paul Lebedev <paul.e.lebedev@gmail.com>

set -ex

rm -rf build
xcodebuild -project Xcode/libusb.xcodeproj -scheme all -configuration Debug -derivedDataPath `pwd`/build/Debug
xcodebuild -project Xcode/libusb.xcodeproj -scheme all -configuration Release -derivedDataPath `pwd`/build/Release
# Archive the build artifacts
cd build/Debug/Build/Products
7z a ../../../libusb-macos-dylib.7z Debug
cd -
cd build/Release/Build/Products
7z a ../../../libusb-macos-dylib.7z Release
