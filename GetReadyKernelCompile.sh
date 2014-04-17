#!/bin/bash

#Written by Daniel Pelikan
#http://digibird1.wordpress.com/


FV=`zgrep "* firmware as of" /usr/share/doc/raspberrypi-bootloader/changelog.Debian.gz \
| head -1 | awk '{ print $5 }'`

mkdir -p k_tmp/linux

wget https://raw.github.com/raspberrypi/firmware/$FV/extra/git_hash \
-O k_tmp/git_hash
wget https://raw.github.com/raspberrypi/firmware/$FV/extra/Module.symvers \
-O k_tmp/Module.symvers

HASH=`cat k_tmp/git_hash`

wget -c https://github.com/raspberrypi/linux/tarball/$HASH \
	-O k_tmp/linux.tar.gz

cd k_tmp
tar -xzf linux.tar.gz

KV=`uname -r` 

sudo mv raspberrypi-linux* /usr/src/linux-source-$KV
sudo ln -s /usr/src/linux-source-$KV /lib/modules/$KV/build

sudo cp Module.symvers /usr/src/linux-source-$KV/

sudo zcat /proc/config.gz > /usr/src/linux-source-$KV/.config

cd /usr/src/linux-source-$KV/
sudo make oldconfig
sudo make prepare
sudo make scripts
