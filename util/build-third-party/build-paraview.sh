#!/bin/bash
# -----------------------------------------------------------------------------
#
# Copyright (C) The BioDynaMo Project.
# All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
#
# See the LICENSE file distributed with this work for details.
# See the NOTICE file distributed with this work for additional information
# regarding copyright ownership.
#
# -----------------------------------------------------------------------------

#####################################
## Building ParaView for BioDynaMo ##
#####################################

if [[ $# -ne 1 ]]; then
  echo "ERROR: Wrong number of arguments.
Description:
  This script builds paraview.
  The archive will be stored in BDM_PROJECT_DIR/build/paraview.tar.gz
Arguments:
  \$1 Paraview version that should be build"
  exit 1
fi

set -e -x

PV_VERSION=$1
shift
BDM_PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../.."
cd $BDM_PROJECT_DIR

# import util functions
. $BDM_PROJECT_DIR/util/installation/common/util.sh

# archive destination dir
DEST_DIR=$BDM_PROJECT_DIR/build
mkdir -p $DEST_DIR
EchoNewStep "Start building ParaView $PV_VERSION. Result will be stored in $DEST_DIR"
# working dir
WORKING_DIR=/data/ahhesam/bdm-build-third-party
mkdir -p $WORKING_DIR || true
cd $WORKING_DIR

## Clone paraview github repository
git clone https://gitlab.kitware.com/paraview/paraview-superbuild.git
cd paraview-superbuild
git fetch origin
git submodule update --init --recursive
git checkout $PV_VERSION
git submodule update --init --recursive

## Generate the cmake files for paraview
mkdir -p ../paraview-build
cd ../paraview-build

cmake \
  -DPARAVIEW_BUILD_EDITION=CATALYST_RENDERING \
  -DCMAKE_INSTALL_PREFIX="../pv-install" \
  -DCMAKE_BUILD_TYPE:STRING="Release" \
  -DENABLE_ospray:BOOL=ON \
  -DENABLE_ospraymaterials:BOOL=ON \
  -DENABLE_paraviewsdk:BOOL=ON \
  -DENABLE_python:BOOL=ON \
  -DENABLE_python3:BOOL=ON \
  -DENABLE_mpi:BOOL=ON \
  -DUSE_SYSTEM_mpi:BOOL=ON \
  ../paraview-superbuild

# ## Compile and install
# make -j$(CPUCount) install

# # patch and bundle
# # TODO(ahmad): Patch is probably not necessary anymore after relying on rpath
# # on OS X. To be investigated.
# if [ `uname` = "Darwin" ]; then
#   ## Patch vtkkwProcessXML-pv5.5
#   # make install does not set the rpath correctly on OSX
#   install_name_tool -add_rpath "@loader_path/../../qt/lib" bin/vtkkwProcessXML-pv5.5
#   install_name_tool -add_rpath "@loader_path/../../../../../qt/lib" bin/vtkkwProcessXML-pv5.5
#   install_name_tool -add_rpath "@loader_path/../lib" bin/vtkkwProcessXML-pv5.5
# fi

# cd install

# ## tar the install directory
# tar -zcf paraview-$PV_VERSION.tar.gz *

# # After untarring the directory tree should like like this:
# # paraview
# #   |-- bin
# #   |-- include
# #   |-- lib
# #   |-- share

# # Step 5: cp to destination directory
# cp paraview-$PV_VERSION.tar.gz $DEST_DIR
