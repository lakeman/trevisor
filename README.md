TreVisor - The TRESOR Hypervisor
================================

TreVisor adds the encryption facilities of [TRESOR][] to [BitVisor][], i. e., we move
TRESOR one layer below the operating system into the hypervisor such that
secure disk encryption runs transparently for the guest OS.

BitVisor is a tiny hypervisor initially designed for mediating I/O
access from a single guest OS. Its implementation is mature enough to
run Windows and Linux, and can be used as a generic platform for
various research and development projects.


Installation
------------

 1. run make config inside the source directory to configure trevisor
 2. run make
 3. copy bitvisor.elf to /boot
 4. cd to /usr/src/bitvisor/boot/login-trevisor
 5. cp bitvisor.conf.tmpl to bitvisor.conf and modify it to your needs
	lba_low and lba_high can be found with fdisk and must match your partition
	set storage.encryptionKey0.place=./StorageKey1
	set storage.conf0.crypto_name=tresor to use tresor encryption module
 6. run make and enter a password that should be used for encryption
 7. copy module1.bin and module2.bin to /boot
 8. create a menuentry for grub
 9. open /etc/grub.d/40_custom and create an entry as follows:
	menuentry "BitVisor" {
	        insmod part_msdos
		insmod ext2
		set root='(hd0,msdos3)'
		multiboot /bitvisor.elf
		module /module1.bin
		module /module2.bin
	}


USB live system
---------------

To create a live USB flash drive use our tools under boot/login-trevisor/live.
With these tools you can generate a Windows install medium or Ubuntu live system
that runs on top of TreVisor.

If you want to create an Ubtuntu system, the thumb drive should be at least 700M.
If you want to create a Windows install medium it should be at least 4G and you
need to have an ISO image of your Windows DVD. 

Furthermore you should have created the files bitvisor.elf, module1.bin and
module2.bin as described above.

1. To create a windows install medium enter this command from a Linux system in
   boot/login-trevisor/live (as root):
	
	./install-win.sh /dev/sdb /tmp/win.iso ../../../bitvisor.elf\
	../module1.bin ../module2.bin

   Warning: all data on /dev/sdb will be deleted.


2. To create an Ubuntu live system the, the ISO image is downloaded automatically.
   If you want to chose a different mirror you have to modify our script. Run (as 
   root):

	./install-ubuntu.sh /dev/sdb ../../../bitvisor.elf\
	../module1.bin ../module2.bin

   Note: The linux tool "parted" is required; you may have to install it.


[TRESOR]:https://www1.informatik.uni-erlangen.de/tresor
[BitVisor]:http://www.bitvisor.org/
