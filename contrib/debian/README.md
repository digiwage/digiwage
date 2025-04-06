
Debian
====================
This directory contains files used to package digiwaged/digiwage-qt
for Debian-based Linux systems. If you compile digiwaged/digiwage-qt yourself, there are some useful files here.

## digiwage: URI support ##


digiwage-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install digiwage-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your digiwage-qt binary to `/usr/bin`
and the `../../share/pixmaps/digiwage128.png` to `/usr/share/pixmaps`

digiwage-qt.protocol (KDE)

