#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

DOGECOIND=${DOGECOIND:-$SRCDIR/dogecoind}
DOGECOINCLI=${DOGECOINCLI:-$SRCDIR/dogecoin-cli}
DOGECOINTX=${DOGECOINTX:-$SRCDIR/dogecoin-tx}
DOGECOINQT=${DOGECOINQT:-$SRCDIR/qt/dogecoin-qt}

[ ! -x $DOGECOIND ] && echo "$DOGECOIND not found or not executable" && exit 1

# The autodetected version git tag can screw up manpage output a little bit
DOGEVERSIONFULL=$($DOGECOINCLI --version | head -n1)
DOGEVER=$(echo $DOGEVERSIONFULL | awk -F'[ -]' '{ print $6 }')
DOGEGIT=$(echo $DOGEVERSIONFULL | awk -F'[ -]' '{ print $7 }')

# Create a footer file with copyright content
# This gets autodetected fine for dogecoind if --version-string is not set,
# but has different outcomes for dogecoin-qt and dogecoin-cli
echo "[COPYRIGHT]" > footer.h2m
$DOGECOIND --version | sed -n '1!p' >> footer.h2m

for cmd in $DOGECOIND $DOGECOINCLI $DOGECOINTX $DOGECOINQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${DOGEVER} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${DOGEGIT}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
