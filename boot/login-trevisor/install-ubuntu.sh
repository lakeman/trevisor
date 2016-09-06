#!/bin/bash

# a live system creator for TreVisor 
# 
# Copyright (C) 2012   Benjamin Taubmann <taubmann.benjamin@informatik.stud.uni-erlangen.de>
#           
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
# 
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
# 
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place - Suite 330, Boston, MA 02111-1307 USA.
 

DISK=$1
PARTITION="${DISK}1"
TMPDIR="/tmp/trevisor-installer"
WINDIR="/tmp/trevisor-win"
PATHTOTREVISOR="$2"
PATHTOMODULE1="$3"
PATHTOMODULE2="$4"

UBUNTUURL="ftp://ftp.uni-erlangen.de/mirrors/ubuntu-releases/12.04/ubuntu-12.04-desktop-amd64.iso"

if (( $# < 4 )) ; then
 echo "$0 path_to_flash path_to_bitvisor.elf path_module1.bin path_module2.bin" >&2
 exit
fi

read -p "If you continue all data on $1 will be erased! Are you sure? [yN] " -n 1
if [[ ! $REPLY =~ ^[Yy]$ ]]
then
    echo ""
    exit 1
fi

#
# Create partitions
#

parted -s $DISK mklabel msdos
if [ $? -ne 0 ]; then 
    echo "parted had some problems!";
   exit; 
fi
parted -s $DISK -- mkpart primary NTFS 1 -1
if [ $? -ne 0 ]; then 
    echo "parted had some problems! ";
   exit; 
fi


mkfs.ntfs -Q $PARTITION
mkdir -p $TMPDIR
mount $PARTITION $TMPDIR

#
# Install and configure grub
#

grub-install --root-directory=$TMPDIR $DISK

UUID=`blkid $PARTITION -o udev | grep ID_FS_UUID= | sed s/.*=//g`

cp grub-ubuntu.cfg $TMPDIR/boot/grub/grub.cfg
sed -i s/UUID_PLACEHOLDER/$UUID/g $TMPDIR/boot/grub/grub.cfg

wget $UBUNTUURL -O $TMPDIR/boot/ubuntu.iso

cp $PATHTOTREVISOR $TMPDIR/boot
cp $PATHTOMODULE1 $TMPDIR/boot
cp $PATHTOMODULE2 $TMPDIR/boot

umount $TMPDIR

