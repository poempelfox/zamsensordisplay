#!/bin/bash

# This scripts generates bitmap fonts for use
# with the font/gui library delivered by waveshare for their
# ePaper display from the Terminus TTF font.
# This will work for a few font sizes that you can select on the
# commandline (until the width exceeds 16, at which point the
# shell magic will break completely).
# This is a pretty messy hack, and I'm very surprised it works as
# well as it does.
# ASCII characters 32 to 126 are exactly as they should be and
# are in the other 'english fonts' in that library, but following
# them we define additional characters that we want included.
# Note also that this will not generate the whole font file,
# you will need to add some boilerplate around it.

FN="/tmp/fontconv.xbm"

BYTESPERCHAR="2"

if [ "$1" == "16" ] ; then
  FONTHEIGHT=16
  POINTSIZE=14
  BYTESPERCHAR=1
elif [ "$1" == "19" ] ; then
  FONTHEIGHT=19
  POINTSIZE=18
elif [ "$1" == "24" ] ; then
  FONTHEIGHT=24
  POINTSIZE=22
elif [ "$1" == "26" ] ; then
  FONTHEIGHT=26
  POINTSIZE=24
elif [ "$1" == "28" ] ; then
  FONTHEIGHT=28
  POINTSIZE=26
elif [ "$1" == "30" ] ; then
  FONTHEIGHT=30
  POINTSIZE=28
elif [ "$1" == "32" ] ; then
  FONTHEIGHT=32
  POINTSIZE=30
else
  echo "Don't know how to handle font height $1."
  echo "Syntax: $0 fontheight"
  echo "Valid values for fontheight are:"
  echo "  16 (16 high /  8 wide)"
  echo "  19 (19 high / 10 wide)"
  echo "  24 (24 high / 12 wide)"
  echo "  26 (26 high / 13 wide)"
  echo "  28 (28 high / 14 wide)"
  echo "  30 (30 high / 15 wide)"
  echo "  32 (32 high / 16 wide)"
  exit 1
fi

# What doesn't work is generating the first character, the space,
# because imagemagick will refuse to create that empty image.
# Luckily the fix is simple, as space is just $FONTHEIGHT times 0x00...
echo "/* Next character:   (space) */"
for n in `seq 1 $FONTHEIGHT`
do
  if [ "$BYTESPERCHAR" == "1" ] ; then
    echo "0x00, //         "
  else
    echo "0x00, 0x00, //                 "
  fi
done

for c in "!" "\"" "#" "\$" "%" "&" "'" "(" ")" "*" "+" "," "-" "." "/" \
         0 1 2 3 4 5 6 7 8 9 ":" ";" "<" "=" ">" "?" "@" \
         A B C D E F G H I J K L M N O P Q R S T U V W X Y Z \
         "[" "\\\\" "]" "^" "_" "\`" \
         a b c d e f g h i j k l m n o p q r s t u v w x y z \
         "{" "|" "}" "~" \
         $'\xc2\xb0'
do
  echo "/* Next character: $c */"
  convert -background white -fill black \
          -font Terminus-\(TTF\) -pointsize $POINTSIZE \
          -colors 2 +antialias label:"${c}" "${FN}" 2>/dev/null
  if [ ! -e "$FN" ] ; then
    echo "Failed to create bitmap from character '$c'."
    exit 1
  fi
  if [ "$BYTESPERCHAR" == "1" ] ; then
    hexvs=`tail -n -2 "${FN}" | sed -e 's/ };//' -e 's/,/ /g' -e 's/0x//g'`
    for l in $hexvs
    do
      rl=`echo $l | rev | tr '0123456789ABCDEF' '084C2A6E195D3B7F'`
      binv=`echo "obase=2; ibase=16; ${rl}" | bc | awk '{printf "%08d\n", $0}' | sed -e 's/0/ /g' -e 's/1/#/g'`
      echo "0x${rl}, // ${binv}"
    done
  else
    hexvs=`grep -v '#define' "${FN}" | grep -v '= {' | sed -e 's/ };//' -e 's/0x\(..\), 0x\(..\),/\1\2/g'`
    for l in $hexvs
    do
      rl=`echo ${l:2:2}${l:0:2} | rev | tr '0123456789ABCDEF' '084C2A6E195D3B7F'`
      binv=`echo "obase=2; ibase=16; ${rl}" | bc | awk '{printf "%16d\n", $0}' | sed -e 's/0/ /g' -e 's/1/#/g'`
      echo "0x${rl:0:2}, 0x${rl:2:2}, // ${binv}"
    done
  fi
  rm -f $FN
done

