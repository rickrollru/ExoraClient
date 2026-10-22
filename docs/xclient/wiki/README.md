# Editor wiki texts

`content.py` holds the articles of the editor wiki in English and Russian. After a change run:

    python3 docs/xclient/wiki/gen.py docs/xclient/wiki/content.py src/game/client/components/xclient/editor/xeditor_wiki_pages.h /tmp/pairs.txt

and add the new English|Russian pairs from `/tmp/pairs.txt` to `ui/lang.cpp`.
