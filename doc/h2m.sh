#!/bin/sh
pacman -Q perl html-xml-utils >/dev/null || exit
cd "$(readlink -zf "$0" | xargs -0 dirname -z)" || exit
exec <evilwm.html >../evilwm.1
cat manpage-header
exec hxnormalize -xe -l 99999 | hxpipe | ./html2man.pl
