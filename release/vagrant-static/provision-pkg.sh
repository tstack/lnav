#!/usr/bin/env bash

# Installs dependencies for running fpm

sudo apt-get update
sudo apt-get install -y ruby2.3 ruby-dev gcc make zip unzip rpm
sudo gem install fpm
