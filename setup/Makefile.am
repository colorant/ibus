# vim:set noet ts=4:
#
# ibus - The Input Bus
#
# Copyright (c) 2007-2008 Huang Peng <shawn.p.huang@gmail.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place, Suite 330,
# Boston, MA  02111-1307  USA

ibussetup_PYTHON = \
	main.py \
	icon.py \
	enginecombobox.py \
	enginetreeview.py \
	engineabout.py \
	keyboardshortcut.py \
	$(NULL)

ibussetup_DATA = \
	setup.glade \
	$(NULL)

bin_SCRIPTS = ibus-setup
ibussetupdir = $(pkgdatadir)/setup

desktopdir = $(datadir)/applications
desktop_in_files = ibus-setup.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

CLEANFILES = \
	*.pyc \
	ibus-setup \
	$(NULL)

EXTRA_DIST = \
	ibus-setup.in \
	setup.glade \
	$(desktop_in_files) \
	$(NULL)

DISTCLEANFILES = \
	$(desktop_DATA) \
	$(NULL)

test:
	$(ENV) \
		PYTHONPATH=$(top_srcdir) \
		IBUS_PREFIX="/usr" \
		$(PYTHON) \
		$(srcdir)/main.py
