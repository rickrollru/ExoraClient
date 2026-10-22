// Generated from the wiki texts: the pages of the editor wiki (xeditor_wiki.cpp). English, translated with XcUi::Tr.
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_WIKI_PAGES_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_WIKI_PAGES_H

namespace XcWiki
{

static const SBlock s_aBlocks_am_what[] = {
	{BLOCK_TEXT, "You draw the map with one plain block, the automapper puts the right tile on every cell by itself: grass on top, edges at the sides, rounded corners, the inside of the rock. It looks at the neighbors of each block and follows rules.", ""},
	{BLOCK_TEXT, "The rules belong to a tileset (a picture). One tileset can have several rule sets, for example \"Default\" and \"No grass\". The rules live in a small text file next to the game files: editor/automap/<tileset>.rules.", ""},
	{BLOCK_HEAD, "Where it is used", ""},
	{BLOCK_TEXT, "A tile layer has the property \"Automapper\": choose a rule set there. Then the automapper of the layer runs on what you draw, and the smart fill tool ([U]) fills a rectangle and makes the edges at once.", ""},
	{BLOCK_TIP, "Many tilesets of the game come with rules. Your own or downloaded tilesets usually have none, and writing a rules file by hand is hard. That is what the automapper builder is for.", ""},
	{BLOCK_HEAD, "How to open the builder", ""},
	{BLOCK_STEPS, "Images tab: click a picture, then \"Automapper builder\" in its card.\nProperties of a tile layer: the \"+\" button next to the automapper choice.\nThe tools menu: \"Automapper builder...\". Or Ctrl+K and type \"automapper\".", ""},
};
static const SBlock s_aBlocks_am_start[] = {
	{BLOCK_TEXT, "For most tilesets the wizard is enough. A whole automapper takes a minute.", ""},
	{BLOCK_STEPS, "Open the builder for your tileset. The \"Wizard\" tab is open.\nClick \"Guess the roles\": the builder fills the roles by itself. Or pick them by hand: the role \"Inside\" is selected, click its tile in the picture.\nCheck the preview on the right. Wrong tile? Select the role on the left and click the right tile (a click on a picked tile takes it back).\nMissing sides are turned from the tiles you picked. If the tileset has grass only on top, turn this off and pick the sides yourself.\nGive the rule set a name and press \"Save the rules\". The selected layer uses the new set at once.", ""},
	{BLOCK_TIP, "The minimum is three tiles: inside, top edge and one outer corner. Everything else can be turned from them.", ""},
	{BLOCK_TIP, "The builder remembers the roles: open it again for the same tileset and the wizard continues where you left.", ""},
};
static const SBlock s_aBlocks_am_roles[] = {
	{BLOCK_TEXT, "A role says where a tile goes. The small picture next to each role shows the cell (in color) and its neighbors: a gray square must be a block, an empty frame must be empty, a dark square does not matter.", ""},
	{BLOCK_ROLE, "Every block gets it first. The other roles replace it where they fit. Needed.", "0"},
	{BLOCK_ROLE, "Empty above: the surface, often with grass.", "1"},
	{BLOCK_ROLE, "Empty below: the ceiling.", "2"},
	{BLOCK_ROLE, "Empty on the left: a wall looking left.", "3"},
	{BLOCK_ROLE, "Empty on the right: a wall looking right.", "4"},
	{BLOCK_ROLE, "Empty above and on the left: the round outer corner.", "5"},
	{BLOCK_ROLE, "Empty above and on the right.", "6"},
	{BLOCK_ROLE, "Empty below and on the left.", "7"},
	{BLOCK_ROLE, "Empty below and on the right.", "8"},
	{BLOCK_ROLE, "Blocks above and on the left, but empty diagonally up-left: the small notch where two walls meet inside.", "9"},
	{BLOCK_ROLE, "The same, empty diagonally up-right.", "10"},
	{BLOCK_ROLE, "The same, empty diagonally down-left.", "11"},
	{BLOCK_ROLE, "The same, empty diagonally down-right.", "12"},
	{BLOCK_ROLE, "A row one block high: empty above and below.", "13"},
	{BLOCK_ROLE, "A column one block wide: empty left and right.", "14"},
	{BLOCK_ROLE, "A block alone: empty on all four sides.", "15"},
	{BLOCK_TIP, "Not every tileset has every role. Leave a role empty and the tile of a more general role stays there (for a platform: the edges and corners).", ""},
	{BLOCK_TIP, "Several tiles in one role are mixed randomly, all equally often. Good for grass or stones that should not repeat.", ""},
};
static const SBlock s_aBlocks_am_turn[] = {
	{BLOCK_TEXT, "With \"Missing sides: turn picked tiles\" on, a role without a tile gets a picked tile turned or mirrored so it fits. The top edge turned by 90° becomes the right edge, mirrored it becomes the left edge, flipped it becomes the bottom. The role shows \"turned automatically\" and a pale picture.", ""},
	{BLOCK_TEXT, "The builder turns tiles exactly the way the game draws them, so the preview shows what you get on the map.", ""},
	{BLOCK_WARN, "Turn it off when the tileset is not the same on all sides: grass only on top, a dark shadow only below, light only from one side. Then pick the bottom and the sides yourself.", ""},
	{BLOCK_TIP, "You can mix: pick the top and the bottom yourself and let the builder turn only the sides.", ""},
};
static const SBlock s_aBlocks_am_guess[] = {
	{BLOCK_TEXT, "\"Guess the roles\" looks at the pictures of all 256 tiles. For each side and corner of a tile it measures if it is see-through, or (in solid tilesets) if its color differs from the inside of the rock. Then every role gets up to three tiles that fit its neighbors best.", ""},
	{BLOCK_WARN, "It is a draft, not magic. Decorations, shadows and special tiles can end up in a wrong role. Always check the preview and fix the roles that look wrong.", ""},
	{BLOCK_TIP, "Changed your mind? \"Undo the guess\" brings the old roles back (until you click a tile).", ""},
	{BLOCK_TEXT, "It works best with tilesets that have see-through edges (most DDNet tilesets) or a clear border color (grass on dirt).", ""},
};
static const SBlock s_aBlocks_am_preview[] = {
	{BLOCK_TEXT, "The preview runs the rules on a small test world: hills, a cave with a hole, floating platforms, a column and single blocks. It changes at once with every click.", ""},
	{BLOCK_STEPS, "Left mouse on the preview: add blocks, drag to draw.\nRight mouse: remove blocks.\n\"Shuffle\": other random choices, to see how several tiles of a role mix.\n\"Reset shape\" brings the test world back, \"Empty\" clears it.", ""},
	{BLOCK_TIP, "A gray cell in the preview is a block that no rule draws yet. In the wizard it means the inside is missing; with rules by hand a rule for every block is missing.", ""},
	{BLOCK_TIP, "On the \"Rules by hand\" page the cells put by the selected rule have a frame. Click through the rules and see what each one does.", ""},
};
static const SBlock s_aBlocks_am_hand[] = {
	{BLOCK_TEXT, "The wizard is enough for simple tilesets. For grass on top of dirt, two blocks thick edges, decorations or several layers of tiles use \"Rules by hand\". There you see every rule of every set of the file, also the sets that came with the game.", ""},
	{BLOCK_HEAD, "A rule is: put this tile when these conditions are true", ""},
	{BLOCK_TEXT, "Every rule has a result (the tile, maybe mirrored or turned) and conditions on the neighbors around the cell, up to two cells far. The grid of 5×5 cells in the middle shows them; the middle cell is the cell itself.", ""},
	{BLOCK_GRID, "This rule puts its tile on a block that has empty above and blocks left and right: a top edge in the middle of a surface.", ".......E...BXB..........."},
	{BLOCK_HEAD, "The default", ""},
	{BLOCK_TEXT, "A rule only works on cells with a block. \"Also on empty cells\" lets it put tiles on empty cells too: for decorations on top of a surface.", ""},
	{BLOCK_TIP, "A rule without conditions puts its tile on every block. The first rule of a set is usually that: the inside.", ""},
};
static const SBlock s_aBlocks_am_cond[] = {
	{BLOCK_TEXT, "Click a cell of the grid to go through the kinds of condition. Right click sets it back to \"any\".", ""},
	{BLOCK_STEPS, "Any (dark): this neighbor does not matter.\nEmpty (frame): there must be nothing.\nBlock (gray): there must be any tile.\nIs (tiles): there must be one of the chosen tiles. Select the cell, then click tiles in the tileset below to add or remove them.\nIs not (crossed tiles): there must not be one of the chosen tiles (empty is fine).", ""},
	{BLOCK_WARN, "Outside the layer counts as a block, not as empty. A block at the border of the map does not get an edge on that side.", ""},
	{BLOCK_TIP, "\"Is\" and \"Is not\" look at the tiles as they were before this run (see \"Order and runs\"). In the first run that is the plain block you drew, usually tile 1.", ""},
};
static const SBlock s_aBlocks_am_order[] = {
	{BLOCK_TEXT, "The rules of a run are tried from top to bottom for every cell. When several rules fit a cell, the last one wins. So general rules go first (the inside), special ones after them (edges, then corners).", ""},
	{BLOCK_STEPS, "Use \"Up\" and \"Down\" to change the order.\n\"Copy\" makes a copy of the selected rule right below it: quick for the four sides of something.\n\"+ Rule\" adds a rule after the selected one.", ""},
	{BLOCK_HEAD, "Runs", ""},
	{BLOCK_TEXT, "A set can have several runs, one after another. Every run sees the result of the run before. The first run makes the rock, a second run can put decorations on what the first one made.", ""},
	{BLOCK_TEXT, "Inside one run the rules look at a copy made before the run started, so they do not disturb each other. The switch next to a run (\"NoLayerCopy\") turns that off: the rules then see the changes of the same run right away. Needed only for special tricks.", ""},
};
static const SBlock s_aBlocks_am_chance[] = {
	{BLOCK_TEXT, "\"Chance\" lets a rule work only on some of the cells it fits. The choice is random but the same every time for the same cell and seed, so the map does not change when you run the automapper again.", ""},
	{BLOCK_HEAD, "Several tiles equally often", ""},
	{BLOCK_TEXT, "For three grass tiles: the first rule with 100%, the second with 50% (1 of 2), the third with 33% (1 of 3). Each later rule replaces the earlier ones on its share of cells, and in the end all three appear equally often. The wizard does exactly this for several tiles in a role.", ""},
	{BLOCK_TIP, "A rare tile (a flower, a crack) with 5–10% makes a big surface look alive.", ""},
	{BLOCK_TIP, "\"Shuffle\" under the preview shows other random choices.", ""},
};
static const SBlock s_aBlocks_am_recipes[] = {
	{BLOCK_HEAD, "Grass only on top", ""},
	{BLOCK_STEPS, "Wizard: pick inside, top edge and the two top outer corners.\nTurn \"Missing sides\" off.\nPick the bottom edge, the left and right edges and the bottom corners from the dirt tiles.", ""},
	{BLOCK_HEAD, "Edges two blocks thick", ""},
	{BLOCK_TEXT, "Rules by hand: a rule with \"empty\" two cells above and \"block\" one cell above puts the second row of the edge.", ""},
	{BLOCK_GRID, "The second row under the surface.", "..E....B....X............"},
	{BLOCK_HEAD, "Decorations on the surface", ""},
	{BLOCK_STEPS, "Add a run (\"+ Run\").\nAdd a rule with the decoration tile, turn on \"Also on empty cells\".\nConditions: the cell itself empty, below it \"is\" the top edge tiles.\nSet a chance of 20–30%.", ""},
	{BLOCK_GRID, "A decoration above the surface (the middle cell must be empty, below it a top edge).", "............Y....T......."},
	{BLOCK_HEAD, "A border of another tileset", ""},
	{BLOCK_TEXT, "The automapper only works with the tiles of one picture. For an unhookable border on a hookable rock use two layers: the rock with its rules, and a second layer with the unhookable tileset and its own rules.", ""},
};
static const SBlock s_aBlocks_am_save[] = {
	{BLOCK_TEXT, "\"Save the rules\" writes the file editor/automap/<tileset>.rules in your DDNet folder. Other rule sets in the file stay; a set with the same name is replaced. The picture reads its rules again and the selected tile layer gets the new set.", ""},
	{BLOCK_TEXT, "On Windows the folder is %APPDATA%\\DDNet\\editor\\automap.", ""},
	{BLOCK_HEAD, "Using the rules", ""},
	{BLOCK_STEPS, "Select a tile layer with this tileset.\nIn its properties choose the rule set under \"Automapper\".\nDraw with tile 1 (any block) and run the automapper of the layer, or use the smart fill [U]: it fills a rectangle and makes the edges at once.", ""},
	{BLOCK_HEAD, "Sharing", ""},
	{BLOCK_TEXT, "Send the .rules file to a friend: he puts it into the same folder. The rules are a normal DDNet file: they also work in the classic DDNet editor and in other clients. The map itself stays a normal map, the rules are not inside it.", ""},
	{BLOCK_WARN, "The file is named after the picture. If you rename the picture in the map, rename the file too, or the rules are not found.", ""},
};
static const SBlock s_aBlocks_am_faq[] = {
	{BLOCK_HEAD, "The preview is gray", ""},
	{BLOCK_TEXT, "No rule draws the blocks: in the wizard pick the \"Inside\" tile; with rules by hand add a rule without conditions at the top.", ""},
	{BLOCK_HEAD, "The edges are wrong on one side", ""},
	{BLOCK_TEXT, "A turned tile does not fit this tileset. Turn off \"Missing sides\" or pick that role by hand.", ""},
	{BLOCK_HEAD, "Corners are not round", ""},
	{BLOCK_TEXT, "With rules by hand the corner rules must come after the edge rules (the last fitting rule wins).", ""},
	{BLOCK_HEAD, "At the map border there is no edge", ""},
	{BLOCK_TEXT, "That is how DDNet works: outside the layer counts as a block. Leave an empty row at the border of the layer.", ""},
	{BLOCK_HEAD, "The layer does not offer my rules", ""},
	{BLOCK_TEXT, "The layer must use the same picture the rules were made for, and the rules must be saved. Then choose the set in the layer properties.", ""},
	{BLOCK_HEAD, "I broke the rules of a game tileset", ""},
	{BLOCK_TEXT, "Your saved file is in your own DDNet folder and wins over the file of the game. Delete it there (editor/automap/<tileset>.rules) and the game rules are back.", ""},
};
static const SBlock s_aBlocks_am_format[] = {
	{BLOCK_TEXT, "The builder writes the usual DDNet format, you never have to write it by hand. This page is for the curious and for reading files of other people.", ""},
	{BLOCK_CODE, "[Grass]\n# a line starting with # is a comment\n\nIndex 1\n\nIndex 16\nPos 0 -1 EMPTY\n\nIndex 4\nPos 0 -1 EMPTY\nPos -1 0 EMPTY\n\nIndex 17 XFLIP\nPos 0 -1 EMPTY\nPos 1 0 EMPTY\nRandom 50%\n\nNewRun\nIndex 70\nPos 0 0 EMPTY\nPos 0 1 INDEX 16 OR 17\nRandom 25%\nNoDefaultRule", ""},
	{BLOCK_STEPS, "[Name] starts a rule set.\nIndex N puts tile N; XFLIP, YFLIP and ROTATE mirror and turn it.\nPos x y says what must be at the neighbor x to the right and y down: EMPTY, FULL, INDEX a OR b (one of these tiles), NOTINDEX a (not this tile).\nRandom 50% (or Random 2 = one of 2) is the chance.\nNoDefaultRule: the rule may also work on empty cells.\nNewRun starts the next run, NoLayerCopy lets a run see its own changes.", ""},
	{BLOCK_TIP, "The builder keeps the roles of the wizard as comments (\"# role ...\"), the game ignores them.", ""},
};

static const SPage s_aPages[] = {
	{"am_what", "What the automapper does", s_aBlocks_am_what, (int)std::size(s_aBlocks_am_what)},
	{"am_start", "Quick start in 5 steps", s_aBlocks_am_start, (int)std::size(s_aBlocks_am_start)},
	{"am_roles", "The 16 roles", s_aBlocks_am_roles, (int)std::size(s_aBlocks_am_roles)},
	{"am_turn", "Turning tiles automatically", s_aBlocks_am_turn, (int)std::size(s_aBlocks_am_turn)},
	{"am_guess", "Guess the roles", s_aBlocks_am_guess, (int)std::size(s_aBlocks_am_guess)},
	{"am_preview", "The preview", s_aBlocks_am_preview, (int)std::size(s_aBlocks_am_preview)},
	{"am_hand", "Rules by hand: how a rule works", s_aBlocks_am_hand, (int)std::size(s_aBlocks_am_hand)},
	{"am_cond", "Conditions", s_aBlocks_am_cond, (int)std::size(s_aBlocks_am_cond)},
	{"am_order", "Order and runs", s_aBlocks_am_order, (int)std::size(s_aBlocks_am_order)},
	{"am_chance", "Chance and variety", s_aBlocks_am_chance, (int)std::size(s_aBlocks_am_chance)},
	{"am_recipes", "Recipes", s_aBlocks_am_recipes, (int)std::size(s_aBlocks_am_recipes)},
	{"am_save", "Saving, sharing, using", s_aBlocks_am_save, (int)std::size(s_aBlocks_am_save)},
	{"am_faq", "Problems and answers", s_aBlocks_am_faq, (int)std::size(s_aBlocks_am_faq)},
	{"am_format", "The rules file", s_aBlocks_am_format, (int)std::size(s_aBlocks_am_format)},
};

} // namespace XcWiki

#endif
