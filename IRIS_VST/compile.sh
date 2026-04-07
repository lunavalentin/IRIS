#!/bin/bash
set -e
mkdir -p /Users/luna/Documents/Research/Codes/IRIS/tmp_build
cd /Users/luna/Documents/Research/Codes/IRIS/tmp_build
cmake /Users/luna/Documents/Research/Codes/IRIS/github_release/IRIS_VST
cmake --build . -j 4
mkdir -p ~/Library/Audio/Plug-Ins/VST3/
mkdir -p ~/Library/Audio/Plug-Ins/Components/
cp -r IRIS4_artefacts/VST3/IRIS4.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -r IRIS4_artefacts/AU/IRIS4.component ~/Library/Audio/Plug-Ins/Components/
rm -rf /Users/luna/Documents/Research/Codes/IRIS/tmp_build
