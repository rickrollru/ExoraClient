# (type, en, ru, extra)  types: head text tip warn steps role grid code
P = []
def page(pid, en, ru, blocks): P.append((pid, en, ru, blocks))
H=lambda en,ru:('head',en,ru,'')
T=lambda en,ru:('text',en,ru,'')
TIP=lambda en,ru:('tip',en,ru,'')
W=lambda en,ru:('warn',en,ru,'')
ST=lambda en,ru:('steps',en,ru,'')
R=lambda role,en,ru:('role',en,ru,str(role))
G=lambda grid,en,ru:('grid',en,ru,grid)
C=lambda code:('code',code,'','')

page('am_what','What the automapper does','Что делает автомаппер',[
 T('You draw the map with one plain block, the automapper puts the right tile on every cell by itself: grass on top, edges at the sides, rounded corners, the inside of the rock. It looks at the neighbors of each block and follows rules.',
   'Ты рисуешь карту одним простым блоком, а автомаппер сам ставит в каждую клетку нужный тайл: траву сверху, края по бокам, скруглённые углы, заливку внутри. Он смотрит на соседей каждого блока и действует по правилам.'),
 T('The rules belong to a tileset (a picture). One tileset can have several rule sets, for example "Default" and "No grass". The rules live in a small text file next to the game files: editor/automap/<tileset>.rules.',
   'Правила принадлежат тайлсету (картинке). У одного тайлсета может быть несколько наборов правил, например «Default» и «Без травы». Правила лежат в маленьком текстовом файле рядом с файлами игры: editor/automap/<тайлсет>.rules.'),
 H('Where it is used','Где он работает'),
 T('A tile layer has the property "Automapper": choose a rule set there. Then the automapper of the layer runs on what you draw, and the smart fill tool ([U]) fills a rectangle and makes the edges at once.',
   'У тайлового слоя есть свойство «Автомаппер»: выбери там набор правил. Тогда автомаппер слоя работает на том, что ты рисуешь, а инструмент умной заливки ([U]) заливает прямоугольник и сразу делает края.'),
 TIP('Many tilesets of the game come with rules. Your own or downloaded tilesets usually have none, and writing a rules file by hand is hard. That is what the automapper builder is for.',
   'У многих тайлсетов игры правила уже есть. У своих или скачанных тайлсетов их обычно нет, а писать файл правил руками сложно. Для этого и есть конструктор автомаппера.'),
 H('How to open the builder','Как открыть конструктор'),
 ST('Images tab: click a picture, then "Automapper builder" in its card.\nProperties of a tile layer: the "+" button next to the automapper choice.\nThe tools menu: "Automapper builder...". Or Ctrl+K and type "automapper".',
    'Вкладка Images: кликни картинку, потом «Конструктор автомаппера» в её карточке.\nСвойства тайлового слоя: кнопка «+» рядом с выбором автомаппера.\nМеню инструментов: «Конструктор автомаппера...». Или Ctrl+K и набрать «автомаппер».'),
])

page('am_start','Quick start in 5 steps','Быстрый старт за 5 шагов',[
 T('For most tilesets the wizard is enough. A whole automapper takes a minute.','Для большинства тайлсетов хватит мастера. Целый автомаппер занимает минуту.'),
 ST('Open the builder for your tileset. The "Wizard" tab is open.\nClick "Guess the roles": the builder fills the roles by itself. Or pick them by hand: the role "Inside" is selected, click its tile in the picture.\nCheck the preview on the right. Wrong tile? Select the role on the left and click the right tile (a click on a picked tile takes it back).\nMissing sides are turned from the tiles you picked. If the tileset has grass only on top, turn this off and pick the sides yourself.\nGive the rule set a name and press "Save the rules". The selected layer uses the new set at once.',
    'Открой конструктор для своего тайлсета. Открыта вкладка «Мастер».\nНажми «Угадать роли» — конструктор сам расставит роли. Или выбирай руками: выбрана роль «Внутри», кликни её тайл на картинке.\nПроверь превью справа. Не тот тайл? Выбери роль слева и кликни нужный тайл (клик по выбранному тайлу — убрать его).\nНедостающие стороны поворачиваются из выбранных тайлов. Если у тайлсета трава только сверху — выключи это и выбери стороны сам.\nДай набору название и нажми «Сохранить правила». Выбранный слой сразу получит новый набор.'),
 TIP('The minimum is three tiles: inside, top edge and one outer corner. Everything else can be turned from them.','Минимум — три тайла: внутри, верхний край и один внешний угол. Всё остальное можно получить поворотом.'),
 TIP('The builder remembers the roles: open it again for the same tileset and the wizard continues where you left.','Конструктор запоминает роли: открой его снова для того же тайлсета — мастер продолжит с того же места.'),
])

page('am_roles','The 16 roles','16 ролей',[
 T('A role says where a tile goes. The small picture next to each role shows the cell (in color) and its neighbors: a gray square must be a block, an empty frame must be empty, a dark square does not matter.',
   'Роль говорит, куда ставится тайл. Маленькая схема у каждой роли показывает клетку (цветом) и её соседей: серый квадрат — там должен быть блок, пустая рамка — там должно быть пусто, тёмный квадрат — неважно.'),
 R(0,'Every block gets it first. The other roles replace it where they fit. Needed.','Ставится в каждый блок первым. Остальные роли заменяют его там, где подходят. Обязательная роль.'),
 R(1,'Empty above: the surface, often with grass.','Сверху пусто: поверхность, часто с травой.'),
 R(2,'Empty below: the ceiling.','Снизу пусто: потолок.'),
 R(3,'Empty on the left: a wall looking left.','Слева пусто: стена, смотрящая влево.'),
 R(4,'Empty on the right: a wall looking right.','Справа пусто: стена, смотрящая вправо.'),
 R(5,'Empty above and on the left: the round outer corner.','Пусто сверху и слева: скруглённый внешний угол.'),
 R(6,'Empty above and on the right.','Пусто сверху и справа.'),
 R(7,'Empty below and on the left.','Пусто снизу и слева.'),
 R(8,'Empty below and on the right.','Пусто снизу и справа.'),
 R(9,'Blocks above and on the left, but empty diagonally up-left: the small notch where two walls meet inside.','Блоки сверху и слева, но пусто по диагонали вверх-влево: маленькая выемка там, где две стены сходятся внутрь.'),
 R(10,'The same, empty diagonally up-right.','То же, пусто по диагонали вверх-вправо.'),
 R(11,'The same, empty diagonally down-left.','То же, пусто по диагонали вниз-влево.'),
 R(12,'The same, empty diagonally down-right.','То же, пусто по диагонали вниз-вправо.'),
 R(13,'A row one block high: empty above and below.','Ряд высотой в один блок: пусто сверху и снизу.'),
 R(14,'A column one block wide: empty left and right.','Столб шириной в один блок: пусто слева и справа.'),
 R(15,'A block alone: empty on all four sides.','Блок сам по себе: пусто со всех четырёх сторон.'),
 TIP('Not every tileset has every role. Leave a role empty and the tile of a more general role stays there (for a platform: the edges and corners).','Не у каждого тайлсета есть все роли. Оставь роль пустой — там останется тайл более общей роли (для платформы — края и углы).'),
 TIP('Several tiles in one role are mixed randomly, all equally often. Good for grass or stones that should not repeat.','Несколько тайлов в одной роли ставятся вперемешку, все одинаково часто. Хорошо для травы или камней, которые не должны повторяться.'),
])

page('am_turn','Turning tiles automatically','Автоматический поворот тайлов',[
 T('With "Missing sides: turn picked tiles" on, a role without a tile gets a picked tile turned or mirrored so it fits. The top edge turned by 90° becomes the right edge, mirrored it becomes the left edge, flipped it becomes the bottom. The role shows "turned automatically" and a pale picture.',
   'Когда включено «Недостающие стороны: поворачивать тайлы», роль без тайла получает выбранный тайл, повёрнутый или отражённый так, чтобы подошёл. Верхний край, повёрнутый на 90°, становится правым, отражённый — левым, перевёрнутый — нижним. У роли написано «повёрнут автоматически» и картинка бледная.'),
 T('The builder turns tiles exactly the way the game draws them, so the preview shows what you get on the map.','Конструктор поворачивает тайлы ровно так, как их рисует игра, поэтому превью показывает то, что будет на карте.'),
 W('Turn it off when the tileset is not the same on all sides: grass only on top, a dark shadow only below, light only from one side. Then pick the bottom and the sides yourself.',
   'Выключай, когда тайлсет не одинаковый со всех сторон: трава только сверху, тень только снизу, свет только с одной стороны. Тогда низ и бока выбери сам.'),
 TIP('You can mix: pick the top and the bottom yourself and let the builder turn only the sides.','Можно смешивать: выбери верх и низ сам, а бока пусть конструктор повернёт.'),
])

page('am_guess','Guess the roles','Угадать роли',[
 T('"Guess the roles" looks at the pictures of all 256 tiles. For each side and corner of a tile it measures if it is see-through, or (in solid tilesets) if its color differs from the inside of the rock. Then every role gets up to three tiles that fit its neighbors best.',
   '«Угадать роли» смотрит на картинки всех 256 тайлов. Для каждой стороны и угла тайла он меряет, прозрачные ли они, или (в сплошных тайлсетах) отличается ли их цвет от заливки. Потом каждая роль получает до трёх тайлов, которые лучше всего подходят под её соседей.'),
 W('It is a draft, not magic. Decorations, shadows and special tiles can end up in a wrong role. Always check the preview and fix the roles that look wrong.',
   'Это черновик, а не магия. Украшения, тени и особые тайлы могут попасть не в ту роль. Всегда проверяй превью и исправляй роли, которые выглядят неправильно.'),
 TIP('Changed your mind? "Undo the guess" brings the old roles back (until you click a tile).','Передумал? «Отменить угадывание» вернёт старые роли (пока ты не кликнул тайл).'),
 T('It works best with tilesets that have see-through edges (most DDNet tilesets) or a clear border color (grass on dirt).','Лучше всего работает с тайлсетами, у которых прозрачные края (большинство тайлсетов DDNet) или явный цвет края (трава на земле).'),
])

page('am_preview','The preview','Превью',[
 T('The preview runs the rules on a small test world: hills, a cave with a hole, floating platforms, a column and single blocks. It changes at once with every click.',
   'Превью прогоняет правила на маленьком тестовом мире: холмы, пещера с дыркой, летающие платформы, столб и одиночные блоки. Оно меняется сразу после каждого клика.'),
 ST('Left mouse on the preview: add blocks, drag to draw.\nRight mouse: remove blocks.\n"Shuffle": other random choices, to see how several tiles of a role mix.\n"Reset shape" brings the test world back, "Empty" clears it.',
    'Левая кнопка на превью: поставить блоки, тяни, чтобы рисовать.\nПравая кнопка: убрать блоки.\n«Перемешать»: другой случайный выбор — видно, как смешиваются несколько тайлов роли.\n«Сбросить фигуру» возвращает тестовый мир, «Пусто» очищает его.'),
 TIP('A gray cell in the preview is a block that no rule draws yet. In the wizard it means the inside is missing; with rules by hand a rule for every block is missing.','Серая клетка в превью — блок, который ещё ни одно правило не рисует. В мастере это значит, что нет «Внутри»; в ручных правилах — нет правила для всех блоков.'),
 TIP('On the "Rules by hand" page the cells put by the selected rule have a frame. Click through the rules and see what each one does.','На вкладке «Правила вручную» клетки, которые поставило выбранное правило, обведены рамкой. Пощёлкай по правилам и увидишь, что делает каждое.'),
])

page('am_hand','Rules by hand: how a rule works','Правила вручную: как работает правило',[
 T('The wizard is enough for simple tilesets. For grass on top of dirt, two blocks thick edges, decorations or several layers of tiles use "Rules by hand". There you see every rule of every set of the file, also the sets that came with the game.',
   'Для простых тайлсетов хватит мастера. Для травы поверх земли, краёв в два блока, украшений или нескольких слоёв тайлов используй «Правила вручную». Там видно каждое правило каждого набора файла, и наборы, которые пришли с игрой.'),
 H('A rule is: put this tile when these conditions are true','Правило — это: поставь этот тайл, когда эти условия выполнены'),
 T('Every rule has a result (the tile, maybe mirrored or turned) and conditions on the neighbors around the cell, up to two cells far. The grid of 5×5 cells in the middle shows them; the middle cell is the cell itself.',
   'У каждого правила есть результат (тайл, может быть отражённый или повёрнутый) и условия на соседей вокруг клетки — до двух клеток в каждую сторону. Сетка 5×5 в середине показывает их; центральная клетка — сама клетка.'),
 G('.....'+'..E..'+'.BXB.'+'.....'+'.....','This rule puts its tile on a block that has empty above and blocks left and right: a top edge in the middle of a surface.','Это правило ставит свой тайл на блок, у которого сверху пусто, а слева и справа блоки: верхний край посреди поверхности.'),
 H('The default','По умолчанию'),
 T('A rule only works on cells with a block. "Also on empty cells" lets it put tiles on empty cells too: for decorations on top of a surface.',
   'Правило работает только на клетках с блоком. «Также на пустых клетках» разрешает ставить тайлы и на пустые клетки: для украшений поверх поверхности.'),
 TIP('A rule without conditions puts its tile on every block. The first rule of a set is usually that: the inside.','Правило без условий ставит тайл на каждый блок. Первое правило набора обычно такое: заливка.'),
])

page('am_cond','Conditions','Условия',[
 T('Click a cell of the grid to go through the kinds of condition. Right click sets it back to "any".','Кликай клетку сетки, чтобы перебирать виды условия. Правый клик возвращает «неважно».'),
 ST('Any (dark): this neighbor does not matter.\nEmpty (frame): there must be nothing.\nBlock (gray): there must be any tile.\nIs (tiles): there must be one of the chosen tiles. Select the cell, then click tiles in the tileset below to add or remove them.\nIs not (crossed tiles): there must not be one of the chosen tiles (empty is fine).',
    'Неважно (тёмная): этот сосед не важен.\nПусто (рамка): там не должно быть ничего.\nБлок (серая): там должен быть любой тайл.\nЭто (тайлы): там должен быть один из выбранных тайлов. Выбери клетку, потом кликай тайлы в тайлсете ниже — добавить или убрать.\nНе это (зачёркнутые тайлы): там не должно быть ни одного из выбранных тайлов (пусто — можно).'),
 W('Outside the layer counts as a block, not as empty. A block at the border of the map does not get an edge on that side.','За краем слоя считается блок, а не пустота. Блок на краю карты не получает край с этой стороны.'),
 TIP('"Is" and "Is not" look at the tiles as they were before this run (see "Order and runs"). In the first run that is the plain block you drew, usually tile 1.','«Это» и «Не это» смотрят на тайлы, какими они были до этого прохода (см. «Порядок и проходы»). В первом проходе это простой блок, который ты нарисовал, обычно тайл 1.'),
])

page('am_order','Order and runs','Порядок и проходы',[
 T('The rules of a run are tried from top to bottom for every cell. When several rules fit a cell, the last one wins. So general rules go first (the inside), special ones after them (edges, then corners).',
   'Правила прохода проверяются сверху вниз для каждой клетки. Если клетке подходят несколько правил, побеждает последнее. Поэтому общие правила идут первыми (заливка), особые — после них (края, потом углы).'),
 ST('Use "Up" and "Down" to change the order.\n"Copy" makes a copy of the selected rule right below it: quick for the four sides of something.\n"+ Rule" adds a rule after the selected one.',
    '«Выше» и «Ниже» меняют порядок.\n«Копия» делает копию выбранного правила прямо под ним: быстро для четырёх сторон чего-нибудь.\n«+ Правило» добавляет правило после выбранного.'),
 H('Runs','Проходы'),
 T('A set can have several runs, one after another. Every run sees the result of the run before. The first run makes the rock, a second run can put decorations on what the first one made.',
   'У набора может быть несколько проходов, один за другим. Каждый проход видит результат предыдущего. Первый проход делает скалу, второй может ставить украшения на то, что сделал первый.'),
 T('Inside one run the rules look at a copy made before the run started, so they do not disturb each other. The switch next to a run ("NoLayerCopy") turns that off: the rules then see the changes of the same run right away. Needed only for special tricks.',
   'Внутри одного прохода правила смотрят на копию, сделанную перед началом прохода, поэтому не мешают друг другу. Переключатель у прохода («NoLayerCopy») выключает это: правила сразу видят изменения того же прохода. Нужно только для особых трюков.'),
])

page('am_chance','Chance and variety','Шанс и разнообразие',[
 T('"Chance" lets a rule work only on some of the cells it fits. The choice is random but the same every time for the same cell and seed, so the map does not change when you run the automapper again.',
   '«Шанс» разрешает правилу срабатывать только на части подходящих клеток. Выбор случайный, но одинаковый каждый раз для той же клетки и сида, поэтому карта не меняется при повторном запуске автомаппера.'),
 H('Several tiles equally often','Несколько тайлов одинаково часто'),
 T('For three grass tiles: the first rule with 100%, the second with 50% (1 of 2), the third with 33% (1 of 3). Each later rule replaces the earlier ones on its share of cells, and in the end all three appear equally often. The wizard does exactly this for several tiles in a role.',
   'Для трёх тайлов травы: первое правило со 100%, второе с 50% (1 из 2), третье с 33% (1 из 3). Каждое следующее правило заменяет предыдущие на своей доле клеток, и в итоге все три встречаются одинаково часто. Мастер делает ровно так для нескольких тайлов в роли.'),
 TIP('A rare tile (a flower, a crack) with 5–10% makes a big surface look alive.','Редкий тайл (цветок, трещина) с шансом 5–10% оживляет большую поверхность.'),
 TIP('"Shuffle" under the preview shows other random choices.','«Перемешать» под превью показывает другой случайный выбор.'),
])

page('am_recipes','Recipes','Рецепты',[
 H('Grass only on top','Трава только сверху'),
 ST('Wizard: pick inside, top edge and the two top outer corners.\nTurn "Missing sides" off.\nPick the bottom edge, the left and right edges and the bottom corners from the dirt tiles.',
    'Мастер: выбери «Внутри», верхний край и два верхних внешних угла.\nВыключи «Недостающие стороны».\nВыбери нижний край, левый и правый края и нижние углы из тайлов земли.'),
 H('Edges two blocks thick','Края толщиной в два блока'),
 T('Rules by hand: a rule with "empty" two cells above and "block" one cell above puts the second row of the edge.','Правила вручную: правило с «пусто» на две клетки выше и «блок» на одну клетку выше ставит второй ряд края.'),
 G('..E..'+'..B..'+'..X..'+'.....'+'.....','The second row under the surface.','Второй ряд под поверхностью.'),
 H('Decorations on the surface','Украшения на поверхности'),
 ST('Add a run ("+ Run").\nAdd a rule with the decoration tile, turn on "Also on empty cells".\nConditions: the cell itself empty, below it "is" the top edge tiles.\nSet a chance of 20–30%.',
    'Добавь проход («+ Проход»).\nДобавь правило с тайлом украшения, включи «Также на пустых клетках».\nУсловия: сама клетка пустая, под ней «это» тайлы верхнего края.\nПоставь шанс 20–30%.'),
 G('.....'+'.....'+'..Y..'+'..T..'+'.....','A decoration above the surface (the middle cell must be empty, below it a top edge).','Украшение над поверхностью (центральная клетка пустая, под ней верхний край).'),
 H('A border of another tileset','Окантовка другим тайлсетом'),
 T('The automapper only works with the tiles of one picture. For an unhookable border on a hookable rock use two layers: the rock with its rules, and a second layer with the unhookable tileset and its own rules.',
   'Автомаппер работает только с тайлами одной картинки. Для нецепляемой окантовки на цепляемой скале используй два слоя: скалу с её правилами и второй слой с нецепляемым тайлсетом и его правилами.'),
])

page('am_save','Saving, sharing, using','Сохранение, обмен, использование',[
 T('"Save the rules" writes the file editor/automap/<tileset>.rules in your DDNet folder. Other rule sets in the file stay; a set with the same name is replaced. The picture reads its rules again and the selected tile layer gets the new set.',
   '«Сохранить правила» записывает файл editor/automap/<тайлсет>.rules в твоей папке DDNet. Другие наборы правил в файле остаются; набор с тем же именем заменяется. Картинка перечитывает правила, а выбранный тайловый слой получает новый набор.'),
 T('On Windows the folder is %APPDATA%\\DDNet\\editor\\automap.','В Windows эта папка — %APPDATA%\\DDNet\\editor\\automap.'),
 H('Using the rules','Как пользоваться правилами'),
 ST('Select a tile layer with this tileset.\nIn its properties choose the rule set under "Automapper".\nDraw with tile 1 (any block) and run the automapper of the layer, or use the smart fill [U]: it fills a rectangle and makes the edges at once.',
    'Выдели тайловый слой с этим тайлсетом.\nВ его свойствах выбери набор правил в «Автомаппер».\nРисуй тайлом 1 (любым блоком) и запускай автомаппер слоя или используй умную заливку [U]: она заливает прямоугольник и сразу делает края.'),
 H('Sharing','Как поделиться'),
 T('Send the .rules file to a friend: he puts it into the same folder. The rules are a normal DDNet file: they also work in the classic DDNet editor and in other clients. The map itself stays a normal map, the rules are not inside it.',
   'Отправь файл .rules другу: пусть положит его в ту же папку. Правила — обычный файл DDNet: они работают и в классическом редакторе DDNet, и в других клиентах. Сама карта остаётся обычной картой, правил внутри неё нет.'),
 W('The file is named after the picture. If you rename the picture in the map, rename the file too, or the rules are not found.','Файл называется по имени картинки. Если переименуешь картинку в карте — переименуй и файл, иначе правила не найдутся.'),
])

page('am_faq','Problems and answers','Проблемы и ответы',[
 H('The preview is gray','Превью серое'),
 T('No rule draws the blocks: in the wizard pick the "Inside" tile; with rules by hand add a rule without conditions at the top.','Ни одно правило не рисует блоки: в мастере выбери тайл «Внутри»; в ручных правилах добавь наверх правило без условий.'),
 H('The edges are wrong on one side','Края неправильные с одной стороны'),
 T('A turned tile does not fit this tileset. Turn off "Missing sides" or pick that role by hand.','Повёрнутый тайл не подходит этому тайлсету. Выключи «Недостающие стороны» или выбери эту роль руками.'),
 H('Corners are not round','Углы не скругляются'),
 T('With rules by hand the corner rules must come after the edge rules (the last fitting rule wins).','В ручных правилах правила углов должны идти после правил краёв (побеждает последнее подходящее правило).'),
 H('At the map border there is no edge','На краю карты нет края'),
 T('That is how DDNet works: outside the layer counts as a block. Leave an empty row at the border of the layer.','Так работает DDNet: за краем слоя считается блок. Оставь пустой ряд на краю слоя.'),
 H('The layer does not offer my rules','Слой не предлагает мои правила'),
 T('The layer must use the same picture the rules were made for, and the rules must be saved. Then choose the set in the layer properties.','Слой должен использовать ту же картинку, для которой сделаны правила, и правила должны быть сохранены. Потом выбери набор в свойствах слоя.'),
 H('I broke the rules of a game tileset','Я сломал правила тайлсета игры'),
 T('Your saved file is in your own DDNet folder and wins over the file of the game. Delete it there (editor/automap/<tileset>.rules) and the game rules are back.','Твой сохранённый файл лежит в твоей папке DDNet и важнее файла игры. Удали его там (editor/automap/<тайлсет>.rules) — и правила игры вернутся.'),
])

page('am_format','The rules file','Файл правил',[
 T('The builder writes the usual DDNet format, you never have to write it by hand. This page is for the curious and for reading files of other people.',
   'Конструктор пишет обычный формат DDNet, писать его руками не нужно. Эта страница — для любопытных и чтобы читать чужие файлы.'),
 C('[Grass]\n# a line starting with # is a comment\n\nIndex 1\n\nIndex 16\nPos 0 -1 EMPTY\n\nIndex 4\nPos 0 -1 EMPTY\nPos -1 0 EMPTY\n\nIndex 17 XFLIP\nPos 0 -1 EMPTY\nPos 1 0 EMPTY\nRandom 50%\n\nNewRun\nIndex 70\nPos 0 0 EMPTY\nPos 0 1 INDEX 16 OR 17\nRandom 25%\nNoDefaultRule'),
 ST('[Name] starts a rule set.\nIndex N puts tile N; XFLIP, YFLIP and ROTATE mirror and turn it.\nPos x y says what must be at the neighbor x to the right and y down: EMPTY, FULL, INDEX a OR b (one of these tiles), NOTINDEX a (not this tile).\nRandom 50% (or Random 2 = one of 2) is the chance.\nNoDefaultRule: the rule may also work on empty cells.\nNewRun starts the next run, NoLayerCopy lets a run see its own changes.',
    '[Имя] начинает набор правил.\nIndex N ставит тайл N; XFLIP, YFLIP и ROTATE отражают и поворачивают его.\nPos x y говорит, что должно быть у соседа на x вправо и y вниз: EMPTY (пусто), FULL (блок), INDEX a OR b (один из тайлов), NOTINDEX a (не этот тайл).\nRandom 50% (или Random 2 = 1 из 2) — шанс.\nNoDefaultRule: правило может срабатывать и на пустых клетках.\nNewRun начинает следующий проход, NoLayerCopy разрешает проходу видеть свои изменения.'),
 TIP('The builder keeps the roles of the wizard as comments ("# role ..."), the game ignores them.','Конструктор хранит роли мастера в комментариях («# role ...»), игра их не замечает.'),
])
