AUTOMAKE_OPTIONS = foreign

man_MANS = rotter.1

rotter.1: rotter.1.txt
	asciidoc -d manpage -b docbook -o rotter.1.xml $(srcdir)/rotter.1.txt
	xmlto man rotter.1.xml && rm rotter.1.xml
	

SUBDIRS = src

EXTRA_DIST = $(man_MANS) autogen.sh rotter.1.txt
