#!/bin/bash

FUR_VERSION="0.6.3"

EXPORT_LANGS=("es" "ru" "pl" "pt_BR")

echo '#
msgid ""
msgstr ""' > po/furnace.pot
echo '"Project-Id-Version: furnace '"$FUR_VERSION"'\n"' >> po/furnace.pot
echo '"MIME-Version: 1.0\n"
"Content-Type: text/plain; charset=UTF-8\n"
"Content-Transfer-Encoding: 8bit\n"
' >> po/furnace.pot

find src/ -type f -regex ".*\(cpp\|h\)$" | xargs xgettext --omit-header -k_ -L C++ -o - >> po/furnace.pot

cd po
for i in ${EXPORT_LANGS[@]}; do
  if [ -e "$i".po ]; then
    echo "merging $i"".po..."
    msgmerge --backup=none -U "$i".po furnace.pot
  else
    echo "creating $i"".po..."
    msginit -i furnace.pot -l "$i".UTF-8 --no-translator
  fi
done
