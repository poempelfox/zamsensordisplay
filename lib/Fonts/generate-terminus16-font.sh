#!/bin/bash

# This scripts generates a 16 high / 8 wide bitmap font for use
# with the font/gui library delivered by waveshare for their
# ePaper display from the Terminus TTF font.
# This is a pretty messy hack, and I'm very surprised it works as
# well as it does.
# ASCII characters 32 to 127 are exactly as they should be and
# are in the other 'english fonts' in that library, but following
# them we define additional characters that we want included.
# Note also that this will not generate the whole font file,
# you will need to add some boilerplate around it.

FN="/tmp/fontconv.xbm"

# What doesn't work is generating the first character, the space,
# because imagemagick will refuse to create that empty image.
# Luckily the fix is simple, as space is just 16 times 0x00...
echo "/* Next character:   (space) */"
for n in `seq 1 16`
do
  echo "0x00, //         "
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
          -font Terminus-\(TTF\) -pointsize 14 \
          -colors 2 +antialias label:"${c}" "${FN}" 2>/dev/null
  if [ ! -e "$FN" ] ; then
    echo "Failed to create bitmap from character '$c'."
    exit 1
  fi
  hexvs=`tail -n -2 "${FN}" | sed -e 's/ };//' -e 's/,/ /g' -e 's/0x//g'`
  for l in $hexvs
  do
    rl=`echo $l | rev | tr '0123456789ABCDEF' '084C2A6E195D3B7F'`
    binv=`echo "obase=2; ibase=16; ${rl}" | bc | awk '{printf "%08d\n", $0}' | sed -e 's/0/ /g' -e 's/1/#/g'`
    echo "0x${rl}, // ${binv}"
  done
  rm -f $FN
done

