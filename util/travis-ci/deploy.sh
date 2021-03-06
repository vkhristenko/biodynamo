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

# assumes working dir = build dir

# helpful tutorial
# https://gist.github.com/willprice/e07efd73fb7f13f917ea

set -e -x

if [ "$TRAVIS_BRANCH" = "master" ] && [ "$TRAVIS_OS_NAME" = "linux" ] && [ "$TRAVIS_PULL_REQUEST" = "false" ]; then
  # set-up git
  git config --global user.email "bdmtravis@gmail.com"
  git config --global user.name "BioDynaMo Travis-CI Bot"

  # Install docker to launch container for gatsby website generation
  # Instructions from: https://www.vultr.com/docs/installing-docker-ce-on-ubuntu-16-04
  sudo apt-get -y install apt-transport-https ca-certificates curl software-properties-common
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
  sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
  sudo apt-get update
  sudo apt-get -y install docker-ce

  BDM_BUILD_DIR=`pwd`

  # Source BioDynaMo prior to building the website for the container to find
  # the generated Doxygen documentation
  source ${BDM_BUILD_DIR}/bin/thisbdm.sh

  make website
  if [ ! $? -eq 0 ]; then
      echo "# MAKE DOC ERROR #"
      echo "The make doc command failed. The deployment was interrupted and the website was not updated."
      exit 1
  fi

  # checkout github pages dir, clean it and recreate folder structure
  cd
  git clone https://github.com/BioDynaMo/biodynamo.github.io.git
  cd biodynamo.github.io
  rm -rf *
  mv $BDM_BUILD_DIR/website/public/* .
  # Add CNAME file to redirect to biodynamo.org
  echo biodynamo.org > CNAME

  # commit
  git add -A
  git commit -m "Update website (Travis build: $TRAVIS_BUILD_NUMBER)"

  # push changes
  set +x
  # NB: DO NOT USE -x here. It would leak the github token to the terminal
  git remote add origin-pages https://${GH_TOKEN}@github.com/BioDynaMo/biodynamo.github.io.git > /dev/null 2>&1
  git push --quiet --set-upstream origin-pages master > /dev/null 2>&1
fi
