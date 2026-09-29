# POINTLESS — guida per Codex

App desktop Qt 6 (C++/Widgets, MinGW) per dithering/halftone/ASCII di immagini e
video, con layer, palette, timeline e export. Questo file è la fonte di verità su
**come si costruisce** e **come è fatta la UI**. Se un valore qui contraddice il
codice, vince il codice — aggiorna questo file.

### Allineamento al codice locale — correzioni audit e ripristino UI 2026-09-07

Le seguenti precisazioni prevalgono sulle descrizioni storiche più sotto:
- `main.cpp` imposta la scala una sola volta a
  `1.28 * primaryScreen()->availableGeometry().width() / 2558`, non sulla
  larghezza corrente della finestra. Ripristino esplicito richiesto dall'utente:
  nessun clamp aggiunto; stretch colonne 0:1:0, misure iniziali 410/1738/410
  scalate, avvio massimizzato senza ripristino degli splitter dell'audit.
  Timeline sulla riga originale, nessun riassetto automatico. Pannello inferiore
  iniziale 150 pixel logici. Non introdurre nuovi stretch o variazioni di font,
  slider e pulsanti: la UI deve comportarsi come prima dell'audit.
- Vincoli UI richiesti nello screenshot del 2026-09-07: minimi laterali
  380 (sinistra) e 410 (destra) in unità Ui; massimo 560 scalato e circa
  30% della finestra, rispettando i minimi. I divisori sono vincolati insieme
  ai widget, senza zone vuote oltre il massimo. Il mode picker ha larghezza
  fissa Ui::px(330), altezza invariata; fa eccezione alle righe a piena larghezza.
  Library: "+" e miniature sempre Ui::px(145), cambia solo il numero di colonne.
  Rimossa la scritta Unsaved changes; il controllo di modifiche non salvate resta.
- ASCII Palette fino a 8 colori usa GPU (griglia quadrata ed istanziata),
  con scelta OkLab come sulla CPU. Braille, liste oltre 8 colori e atlas
  fuori budget conservano il fallback CPU.
- I colori QSS usano token sostituiti in `main.cpp`; i ruoli condivisi sono
  in `Theme.h`. I box ordinari hanno fill trasparente e bordo `#3D3D3D`;
  selezioni e localizzazione usano anche la famiglia lime/oliva. La tabella
  colori storica non descrive integralmente il tema attuale.
- `ControlsPanel` mostra Position, Rotation e Dimensions (box X/Y per
  larghezza/altezza) all'inizio dello scroll di **Image Adjustments**.
  Non esiste più lo slider logaritmico Scale descritto in §6.5/§6.7;
  `scalePct` e `aspectPct` restano i campi del modello.
- La Library vuota mostra una frase e una CTA **import**; il `+` e la griglia
  vengono mostrati dopo il primo media. Vedi `FilmstripWidget.cpp`.
- Halftone dispone anche di impostazioni tonali e rendering GPU: le note
  storiche «niente palette» e «v1 raster-only» non sono più attuali.
- Il formato .less v2 salva immagini, video/sequenze (frame PNG incorporati,
  fps), trim/offset e asset SVG/pattern personalizzati. La lettura mantiene
  compatibilità con v1. Le vecchie versioni dell'app non leggono v2: conservare
  una copia dei vecchi progetti se serve riaprirli con una build precedente.
- ProjectIO usa QSaveFile e validazione prima della sostituzione; il file JSON
  resta limitato a 768 MiB. Il budget di 512 MiB riguarda pixel allocati in RAM.
  Dal 2026-09-07 import video e riapertura dei frame video usano FrameStore:
  fotogrammi a risoluzione nativa in un file temporaneo mappato, mantenuto vivo
  dalle QImage e rimosso dopo l'ultimo riferimento (anche undo/export).
  Nessun limite aggregato di 512 MiB sui video mappati. Resta un limite per
  singolo frame di 512 MiB e serve spazio temporaneo per i pixel decompressi.
  Non è un decoder su richiesta; le pagine mappate possono occupare RAM
  recuperabile dal sistema operativo. Il totale del processo non è limitato.
- Snapshot condiviso al frame per preview/copia/export; import, apertura,
  salvataggio, prerender e export lavorano in background con annullamento
  cooperativo. MP4 usa una pipe di frame raw, senza PNG intermedi.
- Cache raster CPU 256 MiB; cache texture 256 MiB e tre varianti per layer,
  salvo le texture necessarie al frame corrente; atlas ASCII LRU 64 MiB.
  Questi budget non sono un limite alla memoria totale del processo.
- CMakePresets.json espone Profile e CTest. CI Qt 6.11.1/MSVC 2022 esegue test
  e smoke test del pacchetto; versione unica 2.0.0 da CMake, ffmpeg 8.1.1
  verificato SHA-256, licenze/manifest inclusi. Associazione file solo tramite
  POINTLESS.exe --register-file-association, mai ad ogni avvio.

Il rapporto `audit/POINTLESS-audit-2026-09-05.md` elenca problemi e verifiche
dell'audit originale. Lo stato delle correzioni e i limiti verificati sono in
`audit/POINTLESS-correzioni-2026-09-06.md`; le proposte future restano distinte.

---

## 1. Build & run (Qt NON è sul PATH)

**Compilare sempre in `-Profile`, non in `-Debug`.** Vedi §1.1 per il perché.

```bash
# 1. chiudere l'app: il linker fallisce con "Permission denied" sull'exe lockato
#    (l'utente la tiene aperta mentre testa)
Stop-Process -Name "POINTLESS" -Force -ErrorAction SilentlyContinue

# 2. build (PowerShell)
$env:PATH += ";C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin"
cd "build\Desktop_Qt_6_11_1_MinGW_64_bit-Profile"
mingw32-make -j4
```

- Si lavora via **Qt Creator**, kit *Qt 6.11.1 MinGW 64-bit*.
- Filtrare il rumore: `... | Select-String -Pattern "(error:|warning:(?!.*dxcompiler)|Built target POINTLESS$)"`.
  Il warning `dxcompiler` di windeployqt è innocuo e sempre presente.
- Eseguibile: `build\Desktop_Qt_6_11_1_MinGW_64_bit-Profile\POINTLESS.exe`.
  `windeployqt` + il bundle di `ffmpeg.exe` girano da soli a fine build, quindi
  la cartella è sempre completa e l'exe si può lanciare direttamente.

### 1.1 Due dir di build — usare quella giusta

| dir | flag | quando |
|---|---|---|
| `...-Profile` | `-O2 -g` | **default, sempre** |
| `...-Debug` | `-g` (cioè `-O0`) | solo per una sessione di debugger passo-passo |

Stesso codice, solo flag diversi: `-O0` è **5-6× più lento** sui percorsi CPU
(dither a diffusione d'errore misurato 1503 → 253 ms, export, pre-render del
playback). I mode che girano su GPU non ne risentono. Misurato 2026-08-07.

`-Profile` tiene i simboli di debug, quindi stack trace e gdb funzionano
comunque: non c'è motivo di stare in `-Debug` se non si sta letteralmente
steppando riga per riga.

⚠️ Qt Creator conosce **solo** la configurazione "Debug" (`.qtcreator/
CMakeLists.txt.user`) — la dir `-Profile` è stata creata da riga di comando.
Quindi il tasto Esegui dentro Qt Creator lancia ancora la versione lenta.
Finché l'utente non aggiunge la configurazione nell'IDE, l'app va lanciata
dalla dir `-Profile`. Se `-Profile` non esiste (macchina nuova, clone pulito):

```bash
C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build\Desktop_Qt_6_11_1_MinGW_64_bit-Profile ^
  -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
```

**Le release GitHub non sono coinvolte:** `.github/workflows/build.yml` compila
con `cmake --build build --config Release` sotto MSVC (generatore multi-config),
quindi il pacchetto pubblicato è ottimizzato. Non toccare quel workflow
pensando di "sistemare" il build type.

---

## 2. Architettura

```
src/
  main.cpp
  core/        # rendering & dati — nessuna dipendenza da QtWidgets
    Params.h            # MODELLO CENTRALE: Layer, LayerTransform, SessionParams,
                        # Adjustments, HalftoneSettings, DitherSettings, AsciiSettings,
                        # BlendMode, LayerKind
    DitherRenderer.*    # algoritmi di dithering
    DotGridRenderer.*   # griglia di punti (ex "Halftone", GridGenerator per la griglia)
    HalftoneRenderer.*  # retino AM canonico: 4 screen CMYK ad angoli propri
    AsciiRenderer.*     # rendering ASCII
    MosaicRenderer.*    # griglia di tile rettangolari + testo per tono
    BlendCompositor.*   # blend modi Photoshop
    ImageAdjuster.*     # brightness/contrast/gamma/levels/blur/grain/posterize
    PaletteStore.*      # palette salvate
    Animation.* AnimParams.*  # keyframe/timeline
    VideoIO.*           # import/export mp4 via ffmpeg bundlato
  workers/
    RenderWorker.*      # compone il documento: renderLayer() per layer,
                        # placeOnFrame() applica LayerTransform, poi blend
  ui/
    MainWindow.*        # orchestratore: possiede lo stato, instrada i signal
    ControlsPanel.*     # COLONNA SINISTRA (filename, Layers, Transform, Parameters)
    LayersPanel.*       # lista layer (modalità embedded nella colonna + floating)
    AdjustmentsPanel.*  # sezione "Parameters" (slider immagine)
    ModePanel.*         # COLONNA DESTRA (dropdown mode `PopupPicker` + impostazioni del mode)
    PreviewWidget.*     # canvas centrale, drag transform del layer attivo
    TimelineWidget.* FilmstripWidget.* TonalControlsWidget.*
                        # pannello in basso: tab "Timeline" (dopesheet) / "Library"
                        # (FilmstripWidget — griglia sorgenti, vedi §6.8)
    Widgets.*           # widget condivisi + helper (vedi §4)
    UiScale.h           # Ui::px() — scaling globale
assets/
  style.qss            # TUTTO il QSS (design system)
  icons/*.svg          # icone (registrate in src/resources.qrc)
  fonts/FunnelDisplay.ttf
src/resources.qrc      # ogni asset usato a runtime DEVE essere qui
```

**Flusso dati (UI → pixel):** widget emette signal → `MainWindow` aggiorna il
`Layer`/`SessionParams` correnti → `scheduleRender()` → `RenderWorker` rende ogni
layer (`renderLayer`), lo posiziona sul frame (`placeOnFrame`, usa `LayerTransform`)
e fa il blend → immagine al `PreviewWidget`. Le miniature dei layer si aggiornano
con un timer dopo che il gesto si è fermato.

**Persistenza:** `src/core/ProjectIO.*` salva/carica l'intera composizione
(frame, layer, parent, animazione, libreria still-image embeddata come PNG
base64) in un JSON `.less` — Ctrl+S salva (Ctrl+Shift+S forza "Save As"),
Ctrl+O carica e sostituisce la board corrente. Il formato v2 include i frame dei video/sequenze, la frequenza nativa e gli asset
SVG/pattern incorporati. ProjectIO legge anche v1 e usa scrittura atomica.
Aggiungere campi a `LayerTransform`/`Layer`/alle `*Settings` è a basso rischio
per il rendering, ma va specchiato anche nel `toJson`/`fromJson` corrispondente
in `ProjectIO.cpp` (oltre ai soliti `operator==`), o il campo non sopravvive
al giro di salvataggio.

---

## 3. UI — sistema di scaling (leggere PRIMA di toccare qualsiasi dimensione)

Tutta la UI è disegnata in Figma a una **larghezza di riferimento di 2558 px**
(`Ui::kDesignWidth`). A runtime ogni misura è moltiplicata per
`S = 1.28 * larghezzaDisponibilePrimario / 2558`
(valore iniziale; il resize cambia i layout, non la dimensione del testo).

- In C++: **mai** numeri di pixel nudi per le dimensioni → usare `Ui::px(figmaPx)`
  (es. `Ui::px(40)`). `px()` arrotonda `figmaPx * S`.
- In QSS: usare la funzione `s(...)` (es. `font-size: s(18)px;`) — è un
  preprocessore che fa la stessa cosa. I valori QSS **senza** `s()` (es. alcuni
  `border-radius: 6px`) sono px fissi voluti.
- **Tutti i numeri "px" qui sotto e nel QSS sono px-Figma @2558.** Su una finestra
  ~1293px lo scale è ~0.5, quindi `px(48)` ≈ 24 px reali. Quando confronti con uno
  screenshot, ricordati il fattore di scala.

---

## 4. UI — palette colori (hex esatti da `assets/style.qss`)

| Ruolo | Hex |
|---|---|
| Sfondo finestra / `QMainWindow` / barra di stato / mode tab non attivo | `#1E1E1E` |
| Sfondo pannelli (`#sidePanel`, colonne, scroll, `#controlRoot`) | `#272727` |
| Sfondo box/input (DragSpinBox, QLineEdit, QComboBox, slider groove) | `#3B3B3B` |
| Bordo box (default) | `#5D5D5D` |
| Bordo box (hover/focus) | `#828282` |
| Linee divisorie `#bandLine` / `#separator` | `#3B3B3B` (1px) |
| Separatore verticale `#vseparator` | `#161616` (1px) |
| Testo corpo | `#E3E3E3` |
| Testo titolo (`#sectionTitle`, filename) | `#EEEEEE` |
| Testo label parametri (`#paramLabel`) | `#B2B2B2` |
| Testo valore dentro i box (DragSpinBox value) | `#A6A6A6` |
| Testo "spento" (tab, hint) | `#8E8E8E` |
| **Accento arancione** (accentBtn, indicatore drop layer, `selected` palette confirm) | `#FD5A1F` |
| Accento hover / pressed | `#FD6B35` / `#E04E16` |
| Arancione timeline/auto-key (DIVERSO, non confondere) | `#FF6A00` |
| **Blu selezione** (layer selezionato) | `#568AD9` |
| Blu selezione hover | `#5E92E0` |
| Rosso pericolo (trash hover, delete) | `#FD231F` |
| Riempimento slider (sub-page) e handle | `#E3E3E3` (handle hover `#FFFFFF`) |
| Handle scrollbar | `#8E8E8E` (hover `#A6A6A6`) |

Font: **"Funnel Display"** (bundlato), fallback `Segoe UI, Arial`.

---

## 5. UI — tipografia (per `objectName`)

| objectName | uso | size | weight | colore |
|---|---|---|---|---|
| `sectionTitle` | titoli sezione ("Layers", "Transform", "Parameters") | `s(22)` | 700 | `#EEEEEE` |
| `fileTitle` / `fileTitleEdit` | nome file in alto | `s(22)` | 700 | `#EEEEEE` |
| `paramLabel` | etichetta sopra una riga o uno slider ("Position", "Spacing", "Shape"…) — unica dimensione per ogni label di questo tipo, in tutte le modalità | `s(16)` | 500 | `#B2B2B2` |
| **Testo dentro qualunque box** (DragSpinBox value, combo, line edit, bottoni-box Invert/reset/export/accent, spin timeline) | `Ui::kBoxFontPx` | `s(17)` | 500 (600 accent) | varia (i colori restano differenziati) |
| DragSpinBox lettera (`setTextLabel`) | "W/H/X/Y" | `s(17)` | 700 | `#EEEEEE` |

Helper in `Widgets.cpp`: `makeSectionTitle()`, `makeParamLabel()`,
`makeSeparatorLine()` (`#separator`, 1px), `makeIconButton()` (24×24, icona 14×14),
`makeLabeledGroup(label, controllo)` (label + controllo con gap standard).

### 5.1 Theme.h — costanti condivise (USARLE SEMPRE)
`src/ui/Theme.h` è la fonte unica per il ritmo verticale e le misure dei box in
C++ (i colori/font QSS stanno in `assets/style.qss`, l'altra metà del design
system). Mai numeri nudi nel layout: usare
`Ui::kColLeft`(20) / `Ui::kColRight`(60) / `Ui::kTitleBandPadV`(12) /
`Ui::kGapTitleToFirst`(2) / `Ui::kGapLabelToCtrl`(6) / `Ui::kGapRows`(12) /
`Ui::kGapTwinBoxes`(18) / `Ui::kBoxH`(42) / `Ui::kBoxRadius`(8) /
`Ui::kBoxFontPx`(17) / `Ui::kCellW`(58) e le costanti colore `Ui::kCol*`.

---

## 6. UI — REGOLE DI LAYOUT (la fonte delle liti: rispettarle alla lettera)

### 6.1 Margini della colonna e gutter — IL PUNTO CRITICO
Ogni **riga funzionale** (label + controlli) della colonna sinistra ha:
- margine **sinistro = `Ui::px(20)`** → allinea testo e controlli ai titoli.
- margine **destro = `Ui::px(60)`** → il "gutter" dove vivono le icone/le celle
  valore. **DEVE essere 60, non 20.** È la larghezza con cui si allineano *tutte*
  le righe (Frame dimensions, Position, Rotation, Scale, Brightness, …).
- Il margine destro 60 corrisponde al gutter delle icone occhio/`+` nella lista
  layer e alla colonna delle celle valore dei Parameters.

> Se una riga "sfora" a destra rispetto ai Parameters, quasi sempre ha il margine
> destro a 20 invece di 60, oppure una cella più larga di 58. Controlla lì.
> Un'altra causa frequente: una `QLabel` non elidata (es. il nome di un layer)
> il cui sizeHint a piena lunghezza forza il layout oltre lo spazio disponibile,
> spingendo il gutter fuori dalla colonna — usare `ElidedLabel` (`Widgets.h`)
> per qualunque testo di lunghezza variabile in una riga stretch(1).

### 6.2 Righe a piena larghezza e ritmo verticale
I controlli dentro una riga **riempiono** la larghezza fino al gutter (niente
`addStretch` che lascia vuoto a destra):
- 2 box affiancati (X/Y, W/H, angolo/box-rapido) → `addWidget(w, 1)` ciascuno
  (stretch 1, **niente** `setFixedWidth`), spaziatura `Ui::kGapTwinBoxes`(18).
- slider + cella (Scale, parametri) → slider `addWidget(slider, 1)`, poi la cella
  a destra **`Ui::kCellW × Ui::kBoxH`** (58×42), allineata `Qt::AlignVCenter`.

**Ritmo verticale (identico in TUTTE le sezioni, Theme.h):**
titolo→primo controllo = `kGapTitleToFirst`(2, + i 12 di padding banda);
label→suo controllo = `kGapLabelToCtrl`(6) — via `makeLabeledGroup`/`twinBoxGroup`
o il gruppo interno di `SliderRow`; controllo→label successiva = `kGapRows`(12)
come spacing del layout di sezione. Le righe condizionali (Matrix/Scan/Line…)
sono wrappate in un QWidget così lo spacing sparisce quando sono nascoste.

### 6.3 Dimensioni standard dei componenti
- **DragSpinBox / QLineEdit / QComboBox / bottoni-box**: altezza `Ui::kBoxH`
  (42), `border-radius s(8)`, bg `#3B3B3B`, bordo `#5D5D5D` (hover `#828282`).
  Testo interno `s(17)`. Padding interno DragSpinBox `(10,6,10,6)`, spacing 6.
- **Cella valore dei Parameters** (`SliderRow`): `58 × 42` (`kCellW × kBoxH`).
- **Slider**: groove alta `s(7)` (bordo `#5D5D5D`, radius 4), handle `16×16`
  tondo; nelle righe l'area slider è alta `s(30)`.
- **Icon button** (`#iconBtn`): **24×24 px reali fissi, MAI scalati** (min/max-width/
  height nella QSS clampano sempre a 24, a prescindere da qualunque
  `setFixedSize` dato in C++ — vedi §9). Nei header il `+` è costruito a 26×26
  ma finisce comunque clampato a 24. Qualunque wrapper che centri l'icona
  calcolando la sua taglia in C++ deve leggerla **dopo** `icon->ensurePolished()`,
  altrimenti cattura la taglia pre-clamp e l'icona finisce decentrata.
- **Riga layer** (`LayerRow`): altezza `s(52)`, pill selezione `#568AD9`
  (radius `s(10)`), thumb `46×32` (radius `s(5)`), occhio in un gutter da `s(60)`.
  Il nome è un `ElidedLabel` (non una `QLabel` semplice): un nome file lungo
  senza ellissi forzerebbe il layout oltre il gutter, spingendo l'occhio fuori
  dalla colonna.

### 6.4 Titoli di sezione e linee orizzontali
- **Tutte le linee orizzontali sono 1 px LOGICO** (`setFixedHeight(1)` /
  `setHandleWidth(1)`, MAI `Ui::px(1)` che arrotonda a 2 su schermi larghi),
  colore `#3B3B3B` (`#bandLine`/`#separator`, una sola regola QSS). Niente
  `border-top` su widget adiacenti a una bandLine (raddoppia la linea — è il
  bug che aveva il filmstrip).
- Banda titolo nella colonna sinistra (`titleBand`): margini `(20,12,20,12)`
  (titolo centrato → destra a 20, NON 60), racchiusa **sopra e sotto** da una
  `bandLine` (`#bandLine`, 1px `#3B3B3B`).
- Header "Layers": margini `(20,12,0,12)`, titolo + gutter `s(60)` con il `+`
  (nascosto quando il layer attivo non ha layer, es. dopo aver rimosso l'unica
  sorgente da cui dipendevano).
- Riga nome file: margini `(20,14,20,14)`, seguita da una `bandLine`.

### 6.5 Struttura attuale della colonna sinistra (`ControlsPanel.cpp`)
```
[nome file]            margini (20,14,20,14)
bandLine
QSplitter verticale  (#leftSplit, handle s(8), childrenCollapsible=false)
├─ layersPane
│   ├─ header "Layers" + "+"        (20,12,0,12) · gutter 60
│   ├─ bandLine
│   ├─ LayersPanel(embedded)        lista scrollabile, min s(64)
│   └─ Frame dimensions             box (20,10,60,12): label + [W | H] fill
└─ paramsPane
    ├─ Transform: bandLine · titleBand("Transform") · bandLine
    │   └─ box (20,12,60,16) spacing 8:
    │        "Position" + [X | Y] fill
    │        "Rotation" + [box angolo (icona rotation.svg) | box rapido]
    │                     box rapido = rotate90 · mirror_x · mirror_y
    │                     (icone 18px in pulsanti 34×34, distribuite con stretch)
    │        "Scale"    + [slider log | cella 58×42]
    ├─ titleBand("Parameters") · bandLine
    └─ AdjustmentsPanel (min height s(90))   ← unico blocco che si comprime
```

**Splitter / resize "quanti layer mostrare":** la maniglia `#leftSplit` (sopra
"Transform") ridimensiona Layers vs resto. Per evitare il collasso dei box: il
blocco Transform ha altezza fissa e **solo** `AdjustmentsPanel` ha un minimo
piccolo (`s(90)`), così il pannello sotto non può schiacciare Transform.

### 6.6 Colonna destra (`ModePanel.cpp`) — convenzioni parallele
- Sezioni comprimibili (`PanelSection`, in `ModePanel.cpp`); header riga
  `(20,12,24,12)`, body `(20,2,60,14)` → stesso gutter destro 60 dei controlli.
- In cima, il picker del mode (`m_modePick`, un `PopupPicker` — vedi §6.8, non
  più una riga di tab) sostituisce il vecchio `rectTab` row.

### 6.7 Scala (Transform → Scale): semantica
Slider **logaritmico** su `[0.1× .. 10×]`, centrato a `1.0×` (slider 0..1000,
500 = 1×). La cella accetta **qualsiasi** valore positivo digitato (anche fuori
range slider). Internamente `LayerTransform::scalePct` è in **percentuale**
(100 = 1×); la cella mostra il **moltiplicatore**.

`LayerTransform::aspectPct` (100 = aspetto della sorgente) è lo **stretch solo
in Y** sopra a `scalePct`: lo scrivono i drag dai lati sul canvas, lo slider
Scale non lo tocca. Ogni matrice di piazzamento e ogni bounding-quad DEVE
passare da `layerScaleXY(tf)` (Params.h) — `placeOnFrame`, `layerMatrix`,
`placeOnFramePreview`, `PreviewWidget::quadFrame`; un punto dimenticato disegna
il layer con l'altezza sbagliata.

**I simboli NON si stretchano** (stesso principio della scala uniforme). Una
scala non uniforme si spezza in due: `aspectBakeSize`/`prerenderAspect`
(RenderWorker) pre-stirano il **raster di lavoro** del rapporto fra i due assi e
lasciano al piazzamento una scala **uniforme**, che `compensateSymbolScale`
annulla come sempre → punti tondi, foto stirata. L'asse con la scala minore fa
da resto uniforme, così il pre-stretch ingrandisce e basta (cap alla max dim,
mai sotto la risoluzione nativa). Sul path GPU il pre-stretch è il `bakeBase`
non quadrato del content pass. `effectivePlacementTf(layer, srcSize)` ricalcola
il piazzamento residuo dei **due** bake (aspect + frame-res): usarla sempre al
posto di leggere `transform` grezzo, o una cache hit piazza il layer sbagliato.

**Handle sul canvas (stile Figma, `PreviewWidget::handleHit`)** — niente più
pallina di rotazione: tutto il contorno è afferrabile. Corner = scala sempre
proporzionale (origine allo spigolo opposto, con **Alt** al centro); lato =
stretch su quell'asse (con **Shift** proporzionale, con **Alt** simmetrico dal
centro); anello di `kRotZone`(16 px) appena **fuori** da uno spigolo = rotazione,
col cursore che diventa `rotation.svg`. Hit-test e matematica del drag stanno
negli assi **locali** del layer (`toLocal`/`fromLocal`), non in frame space. Il
gizmo di gruppo (≥2 layer) resta solo uniforme: corner scala, anello ruota.

Flip = scala negativa in
`RenderWorker::placeOnFrame` (`flipH` = specchio asse y / sinistra-destra,
`flipV` = asse x / alto-basso). "Ruota 90°" incrementa `rotation` di 90 con wrap
in `[-180,180]`.

**Dimensioni simbolo = px del FRAME.** Dot Grid `grid.spacing`/`pointSpacing`,
halftone `spacing`, ascii `cellSize`, dither `pixelSize` e — dai cinque mode
7/20 — motionBlur `distance`, thermal `blur`, blueprint `lineWidth`, glitch
`blockSize`/`channelShift` sono tutti espressi in pixel del frame finale:
`compensateSymbolScale` (RenderWorker) annulla la scala di piazzamento del layer
prima del render (raster e SVG), quindi scalare un layer scala la foto, non i
simboli. L'`inputDpi` halftone è compensato allo stesso modo nel branch che
riscala l'immagine di lavoro: è un puro controllo di qualità/risoluzione di
campionamento e **non** cambia più la dimensione dei simboli.

### 6.8 Dropdown condiviso (`PopupPicker`, `Widgets.*`) — usalo sempre, non un `QComboBox`
Ogni menu a tendina "di sistema" (Algorithm, Shape, Grid, Fusion, Charset,
Font, mode picker, formato di Export…) è un `PopupPicker`, non un
`QComboBox`/`NoWheelComboBox` nativo: stessa altezza box (`kBoxH`), popup a
riquadri con eventuali intestazioni a pillola, si apre **a sinistra**
dell'ancestor `sidePanel` (la colonna destra) così le liste lunghe hanno
spazio sopra il canvas. Costruzione: `new PopupPicker(colonne)` +
`setEntries({ {value, label, header, tooltip}, ... })` + `setValue(...)`;
`onSelected` per il pick utente (non richiamato da `setValue`). Un
`QComboBox` nativo qui è quasi sempre un refuso da correggere (vecchio
menu Qt, non allineato al resto).

### 6.9 Library (`FilmstripWidget.cpp`) — griglia sorgenti del tab "Library"
Bottone "+" (in alto a sinistra, quadrato, fuori dalla scrollarea → resta
sempre visibile mentre la griglia sotto scrolla) + griglia quadrata di
thumbnail (drag→layer, doppio click→layer, ✕ in hover→rimuove dalla
libreria). Punti da ricordare se tocchi questo file:
- Il bottone "+" e ogni thumbnail hanno taglia fissa Ui::px(145), indipendente
  dalla larghezza dei pannelli. computeColumns() usa la larghezza della Library
  solo per decidere quante colonne entrano (minimo una); applyCellSizesOnly()
  applica la misura fissa. Non reintrodurre ridimensionamento proporzionale
  delle celle né stretch delle colonne della griglia.
- `m_thumbLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft)` è
  necessario: `setWidgetResizable(true)` stira `m_thumbRow` a tutta
  l'altezza della viewport, e senza quell'allineamento esplicito Qt
  centra verticalmente il contenuto della grid invece di tenerlo
  incollato in alto (evidente con poche righe: la griglia "galleggia" a
  metà invece di stare accanto al bottone "+").

---

## 7. Icone & risorse
- SVG in `assets/icons/`, **registrare ogni icona** in `src/resources.qrc`
  (`<file alias="icons/NAME.svg">../assets/icons/NAME.svg</file>`), usare via
  `:/icons/NAME.svg`.
- Le icone Transform (`rotation`, `rotate90`, `mirror_x`, `mirror_y`) usano
  stroke `#E4E4E4`, nessun fill di sfondo.
- Lo stile vive solo in `assets/style.qss` (selettori per `objectName`). Per
  applicare uno stile dinamico: `setProperty(...)` + `style()->unpolish/polish`.

---

## 8. Verifica visiva
- Catturare con **PrintWindow** (DC della finestra, flag `2`), NON `CopyFromScreen`:
  setup multi-monitor con DPI misti (primario al 200%, app spesso su monitor a Y
  negativo) → una copia a schermo intero prende il monitor sbagliato. PrintWindow
  rende la finestra a prescindere da focus/monitor.
- Schermo basso? Ridimensionare la finestra **più alta dello schermo**: PrintWindow
  cattura comunque l'intero contenuto. Il bitmap può essere DPI-virtualizzato (1× o
  2×): ritaglia e fai `Read` del PNG, **non** fidarti delle coordinate per click
  sintetici (inaffidabili qui).
- **Evitare di iniettare click/input**: l'utente è spesso alla macchina, meglio
  chiedergli di provare l'interazione.

Snippet PowerShell (cattura colonna sinistra alta):
```powershell
$p = Get-Process POINTLESS | ? { $_.MainWindowHandle -ne 0 } | select -First 1
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int ht,bool r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
}
"@
Add-Type -AssemblyName System.Drawing
$h = $p.MainWindowHandle
[W]::MoveWindow($h,0,0,560,1180,$true) | Out-Null; Start-Sleep 2
$bmp = New-Object System.Drawing.Bitmap 560,1180
$g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
[W]::PrintWindow($h,$dc,2) | Out-Null; $g.ReleaseHdc($dc)
$bmp.Save("$PWD\_v.png")   # poi: Read _v.png
```

**Debug a runtime:** è un'app GUI-subsystem → `qDebug`/`qWarning` **non** arrivano
a stderr (`RedirectStandardError` dà file vuoto). Per loggare: scrivere su file da
codice (`QFile` append), rebuild, leggere. Per esercitare un'opzione raggiungibile
solo via combo/popup: cambiare temporaneamente il **default** in `Params.h`,
rebuild, catturare, poi ripristinare.

---

## 9. Gotcha
- Pattern signal "silenzioso": i setter di `ControlsPanel` usano `m_updating`
  per non riemettere durante l'aggiornamento programmatico — rispettarlo.
- Slider/combo custom (`NoWheelSlider`, `NoWheelComboBox`) ignorano la rotella di
  proposito; il Tab si ferma solo sulle celle numeriche.
- Aggiungendo campi a struct in `Params.h`, aggiornare i relativi `operator==`
  (servono per dirty-check / undo).
- Le miniature layer applicano gli adjustment per-layer su una copia ridotta:
  cambi al rendering vanno riflessi sia in `RenderWorker` sia (se serve) nel thumb.
- **`objectName("iconBtn")` clampa SEMPRE a 24×24** (`min/max-width/height`
  in QSS), a prescindere da qualunque `setFixedSize`/`setMinimumSize` dato in
  C++ — la QSS vince. Se ti serve un bottone-icona più grande (es. i tre
  pulsanti rotate90/mirror_x/mirror_y in ControlsPanel), dagli un
  `objectName` proprio (es. `quickIconBtn`) con la sua regola QSS, non
  riusare `iconBtn` e provare a sovrascriverne la taglia in C++ — non
  funziona ed è silenzioso (nessun errore, il bottone resta piccolo).

---

## 10. Feature principali (cosa esiste già)

- **Cinque mode di rendering** (`LayerKind`): **Dot Grid** (ex "Halftone":
  griglia di punti area-corretti, forme multiple, jitter, localizzazione),
  **Dither** (diffusione/ordinato/threshold), **ASCII** (glifi per copertura
  d'inchiostro + metriche font reali), **Mosaic** (griglia di tile
  rettangolari, un colore/testo per tono — nome provvisorio, l'utente non ha
  ancora scelto quello definitivo), **Halftone** (retino AM canonico: 4
  screen CMYK separati con angolo proprio, default K=45 C=15 M=75 Y=0, forme
  Round/Square/Line, inversione punto→buco oltre 50% copertura, niente
  palette/loc, v1 raster-only). Il picker in cima alla colonna destra è un
  `PopupPicker` (§6.8), non tab.
  ⚠ Un **Bitmap** mode (griglia pixel chunky + quantizzazione hard) è stato
  aggiunto e poi rimosso lo stesso giorno: si sovrapponeva concettualmente a
  Dither con `algorithm=Threshold` + `pixelSize` + `levels` bassi (già tutti
  presenti in `DitherSettings`), zero valore nuovo. Se torna in mente "un
  mode bitmap/pixelato" → è già coperto da Dither, non ricrearlo da zero.
  ⚠ Un **Voronoi** mode (stippling: griglia jitterata di punti, cerchio
  pieno per punto) è stato aggiunto e rimosso lo stesso giorno su richiesta
  dell'utente (non piaceva il risultato visivo) — mai stato verificato
  visivamente prima della rimozione.
  ⚠ **Thermal, Blueprint, Glitch, Metal Emboss e Motion Blur** sono stati
  rimossi il 7/21 su richiesta dell'utente: i primi 4 (whole-image-filter GPU,
  con relativi `*Renderer.h/.cpp` e shader `filter.vert/.frag`) giudicati non
  coerenti con l'identità core del prodotto (dithering/halftone/ASCII);
  Motion Blur rimosso a seguire per lo stesso motivo. Priorità attuale è
  lanciare bene il prodotto core prima di aggiungere altre mode. Sorgenti
  spostati (non cancellati) in `src/archived_modes/` (core/ + gpu_shaders/),
  fuori da `CMakeLists.txt` — reintegrabili in futuro ripetendo la checklist
  §10.1 al contrario. Stessa sorte già toccata a Cross Hatch/Edge Detect/Low
  Poly/Engraving (mai arrivati a shippare in `main`, restavano solo in
  questo file come documentazione disallineata dal codice — rimossa insieme
  al resto per evitare confusione futura).
  ⚠ Chiavi JSON `.less`: la chiave `"halftone"` sul Layer è CONGELATA e
  appartiene a **Dot Grid** (retrocompatibilità pre-rename); la nuova
  Halftone CMYK usa `"halftoneAm"`.

### 10.1 Checklist: aggiungere un nuovo mode di rendering
Ogni punto tocca file diverso: saltarne uno crea bug silenzioso (undo rotto,
campo non salva, valore clampato). Ordine consigliato:
1. **Params.h**: nuovo `RenderMode`/`LayerKind` + `layerKindForMode`/
   `modeForLayerKind`/`layerKindName` + nuova `XSettings` struct + suo
   `operator==`; aggiungere campo `XSettings x;` al `Layer`.
2. **core/XRenderer.h/.cpp**: l'algoritmo vero — unica parte non boilerplate.
3. **RenderWorker.cpp**: case nello switch di `renderLayer()`.
4. **ProjectIO.cpp**: `toJson`/`fromJson` per `XSettings` — scegliere la
   chiave JSON definitiva subito (congelarla appena shippata, vedi nota
   halftone/dotgrid sopra).
5. **ModePanel.cpp**: entry nel `PopupPicker` del mode + sezione parametri
   (segue §6.6/§6.8 — mai `QComboBox` nativo).
6. **AnimParams/Animation**: se parametri animabili, `ParamDesc` con range
   che DEVE combaciare l'UI (vedi [[anim-param-ranges]] in memoria — bug
   ricorrente se disallineato).
7. **LocParam/LocMap** (opzionale): localizzazione per-parametro, vedi §10
   sotto — non tutti i mode ce l'hanno (Mosaic escluso finora).
8. Thumbnail/preview: di solito gratis se passa da `renderLayer`.

- **Localizzazione per-parametro** (14 parametri fra i tre mode "storici",
  Mosaic escluso per ora): un punto per parametro, editabile sul canvas,
  toggle overlay con **'H'**. Ogni punto abilitato **maschera** l'effetto del
  layer (fuori dal cerchio non c'è nulla) e scala quel parametro (rotellina
  dentro il cerchio, 0.1×..10×). Vedi `LocParam`/`LocMap`/`LocField` in
  Params.h, `LocDots` in ModePanel.cpp.
- **Layer** con blend modi Photoshop (`BlendCompositor`), visibilità, rinomina,
  riordino drag&drop, trash, **lock** (`Layer.locked` — canvas lo ignora del
  tutto: hit-test, selezione, drag, handle; i box Transform in colonna
  sinistra lo editano comunque); ogni layer ha proprio `LayerTransform` e
  `Adjustments`.
- **Frame**: documento W×H su cui i layer sono composti via `LayerTransform`.
- **Correttezza tonale gamma-correct SEMPRE attiva** (no toggle): luce lineare per
  le *quantità* (copertura, area punto, error diffusion), luma percettiva solo per
  *tono/selezione glifo* (così gli slider Levels 0..255 mantengono significato).
  Helper in `src/core/ColorMath.h` (LUT sRGB↔linear).
- **Palette dithering**: match colore più vicino in **OkLab**; estrazione
  median-cut dall'immagine (`PaletteStore::fromImage`, cap 8 colori); UI in
  `TonalControlsWidget` (bottone "Extract from image" abilita la modalità Palette).
- **Animazione/Timeline** (pure Qt, no dipendenze): keyframe su ~50 parametri
  numerici (`AnimParams`/`Animation`), dopesheet stile Blender (`TimelineWidget`),
  auto-key, easing presets, copy/paste key, playback con cache pre-renderizzata.
- **Video mp4** import/export via **ffmpeg bundlato** (`tools/ffmpeg.exe`,
  `src/core/VideoIO.*`); export H.264/yuv420p, solo video. Import anche sequenze PNG.

## 11. Priorità di prodotto (ordine concordato)

1. **Qualità di output dei mode** *(in corso)* — perfezionare il processing prima
   della UX. Principio gamma-correct sopra.
2. **Palette dithering reale** *(quasi fatto)* — TODO: import palette esterne
   (Lospec .hex/.gpl); Dither `ImageColors` è ancora 2 livelli/canale.
3. **UX polish** *(iniziato — commit "begin of UI unification")* — non più
   esplicitamente rimandato: si sta correggendo box più stretti/corti del
   dovuto, dropdown nativi rimasti da convertire a `PopupPicker` (§6.8),
   allineamenti/hover che non arrivano al bordo, ecc. Reset per-mode e
   preset/template restano da fare.

**Deferred:** animare colori/enum/bool, editor a curve bézier reale (ora solo
easing presets), audio nell'export video.
