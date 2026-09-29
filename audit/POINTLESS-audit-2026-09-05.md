# Audit POINTLESS — 5 settembre 2026

L'app compila in Profile e contiene già ottimizzazioni sensate. Il rischio principale per il lancio è l'affidabilità del documento e della corrispondenza fra anteprima, animazione ed export. Seguono consumo di memoria, operazioni bloccanti e verifiche della distribuzione.

Audit dello stato locale, incluse le modifiche non committate già presenti. Nessuna correzione al codice applicativo eseguita. “Riprodotto” indica una prova eseguita sul codice corrente; “verificato nel codice” indica un percorso deterministico ispezionato; “rischio” indica un meccanismo credibile senza riproduzione del guasto finale. P1 = da risolvere prima del lancio, P2 = intervento successivo. Le priorità sono una valutazione dell'audit.

**Verifiche effettuate**

- Build Qt 6.11.1 MinGW, Profile `-O2 -g`: riuscita. Build incrementale, non una compilazione pulita della release MSVC.
- `nearest_glyph_check`: 159.951 confronti, zero discrepanze.
- `export_frame_check`: passato; dimensioni 80×40 e ritaglio centrale verificati.
- `render_golden`: generati 14 output dei cinque mode e della catena adjustment. È uno smoke test; non è un confronto con immagini di riferimento approvate.
- Prove mirate su documenti sintetici, SVG, undo, timeline, richieste concorrenti e conversioni della UI.
- Layout renderizzato fuori schermo a 1920×1080 e 1280×720, con scala corrispondente a un primario largo 1920 pixel logici. Nessuna interazione con finestre dell'utente.
- Non verificati: resa GPU su hardware diversi, comportamento DPI passando fra monitor fisici, installer/release in macchina pulita, carichi prolungati e benchmark comparativi. Nessuna percentuale di accelerazione è promessa.

**Problemi principali di correttezza**

1. **P1 — Aprire un progetto elimina le modifiche correnti senza richiesta di salvataggio. Riprodotto.**
   `openProjectFromPath()` carica il file e poi svuota board e libreria senza chiamare `isDirty()`. La prova parte da un documento modificato: l'apertura termina con zero layer, zero media e stato pulito. La combinazione con il problema 6 rende sufficiente un file contenente `{}`.
   Intervento: una sola procedura di sostituzione documento, con Save/Discard/Cancel, usata da tutti gli ingressi. Validare il nuovo documento prima della sostituzione.
   [MainWindow.cpp:2789](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2789).

2. **P1 — Titolo e libreria non rendono il progetto “modificato”. Riprodotto.**
   Dopo import di una sorgente non ancora posizionata e modifica del titolo, `isDirty()` resta falso. Il confronto include solo parametri e animazione; titolo e media vengono salvati ma non partecipano al rilevamento modifiche. Chiudere può perdere queste modifiche senza avviso.
   Intervento: stato salvato completo o revisione del documento che includa tutte le mutazioni persistenti.
   [MainWindow.cpp:2830](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2830), [MainWindow.h:120](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.h:120).

3. **P1 — Undo della rimozione di una sorgente ripristina layer orfani. Riprodotto.**
   La cancellazione elimina `board.media[mediaId]`; gli snapshot contengono solo `SessionParams` e `Animation`. Risultato della prova: un layer ripristinato, sorgente assente. A seconda delle altre sorgenti, il rendering può restare vuoto o usare il fallback del documento.
   Intervento: rendere la rimozione un'operazione reversibile che conservi il media; sfruttare la condivisione dei buffer QImage, evitando copie profonde inutili.
   [MainWindow.cpp:3021](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:3021), [MainWindow.cpp:2505](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2505).

4. **P1 — Salvataggio non atomico e successo non verificato. Verificato nel codice.**
   `QFile` apre direttamente la destinazione in WriteOnly; il risultato di `write()` non viene controllato. Un errore dopo l'apertura può lasciare il progetto precedente troncato e restituire successo. Non ho simulato un disco pieno.
   Intervento: `QSaveFile`, controllo della serializzazione delle immagini, della scrittura e di `commit()`. Anche il fallimento finale deve lasciare il documento dirty. [QSaveFile, documentazione Qt](https://doc.qt.io/qt-6/qsavefile.html).
   [ProjectIO.cpp:482](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/ProjectIO.cpp:482).

5. **P1 — Salvare un progetto video perde media, layer e track e marca comunque la sessione come salvata. Verificato nel codice.**
   Il filtro è intenzionale, ma il contratto UX è pericoloso: compare soltanto un messaggio di stato “Saved (skipped…)”, poi `m_savedParams` prende lo stato completo in memoria. “Save” nella finestra di chiusura può quindi chiudere una sessione non ricostruibile dal file.
   Intervento minimo: avviso esplicito prima del salvataggio incompleto, senza presentarlo come salvataggio completo. Soluzione definitiva: media esterni con relink oppure progetto che li includa. Non basta una scritta transitoria.
   [MainWindow.cpp:2711](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2711).

6. **P1 — Il loader accetta documenti non validi e ignora la versione. Riprodotto.**
   `{}` viene accettato. Anche `formatVersion=999`, `frameW=-99`, `frameH=2147483647` vengono accettati senza errore. Non vengono validate integrità dei media, riferimenti/ID, enum e range. Questo non dimostra da solo un crash, ma ammette stati non gestiti e allocazioni sproporzionate nei percorsi successivi.
   Intervento: validazione centralizzata dello schema e dei limiti, versioni supportate, riferimenti coerenti, immagini decodificabili; errore comprensibile e board precedente intatta.
   [ProjectIO.cpp:506](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/ProjectIO.cpp:506), [ProjectIO.cpp:420](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/ProjectIO.cpp:420).

7. **P1 — SVG non preserva le fusioni e può segnalare un export inesistente. Riprodotto.**
   Con layer `#8040C0` in Multiply su sfondo `#808080`, il renderer raster produce `#402060`, mentre l'SVG riletto produce `#8040C0`. Il fallback rasterizza il singolo layer e lo disegna con la composizione normale, perdendo la fusione con ciò che sta sotto. Inoltre un percorso dentro una directory inesistente restituisce `true`.
   Intervento: rasterizzare il gruppo di composizione necessario, o l'intero documento quando richiesto per fedeltà, e controllare apertura del dispositivo/painter e scrittura. Esplicitare quali parti restano vettoriali.
   [RenderWorker.cpp:840](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/workers/RenderWorker.cpp:840), [RenderWorker.cpp:879](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/workers/RenderWorker.cpp:879).

8. **P1 — Copia immagine su GPU usa uno stato diverso dall'export. Verificato nel codice.**
   `copyToClipboard()` passa `img.state` senza `paramsAtFrame()` e senza `bakeGroupVisibility()`, mentre risolve i media al playhead. In un documento animato o con gruppo nascosto, Ctrl+C può copiare parametri di base o contenuti invisibili in anteprima.
   Intervento: condividere la risoluzione dello snapshot del documento fra copia, PNG/JPG, SVG e video.
   [MainWindow.cpp:2597](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2597), [MainWindow.cpp:3095](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:3095).

9. **P1 — Il worker pubblica anche risultati di richieste già superate. Riprodotto.**
   La prova richiede rosso e subito blu: dopo la richiesta blu arriva prima il risultato rosso, poi quello blu. I segnali non trasportano una revisione del documento. Il consumer accetta questi risultati; i pass fast/full indipendenti rendono possibile che un vecchio full copra un'anteprima più recente. Il controllo dei layer vuoti copre solo un caso particolare.
   Intervento: ID progressivo per richiesta/documento, scarto dei risultati obsoleti e cancellazione cooperativa dei lavori lunghi quando possibile.
   [RenderWorker.cpp:1244](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/workers/RenderWorker.cpp:1244), [MainWindow.cpp:2460](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2460).

10. **P1 — Chiusura durante un render: rischio di accesso a oggetto distrutto. Rischio verificato nella gestione della vita degli oggetti, crash non riprodotto.**
    Le lambda catturano `this` e usano cache/mutex del worker; il distruttore è default e non attende i lavori. Nell'header Qt installato, il distruttore di QFutureWatcher scollega le notifiche e non attende il calcolo. Chiamare semplicemente `cancel()` sui task base di `QtConcurrent::run` non è una soluzione sufficiente. [Documentazione Qt](https://doc.qt.io/qt-6/qfuturewatcher.html).
    Intervento: fermare nuove richieste, assicurare la durata dello stato condiviso e completare/terminare cooperativamente i job prima della distruzione.
    [RenderWorker.cpp:1006](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/workers/RenderWorker.cpp:1006), [RenderWorker.cpp:1193](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/workers/RenderWorker.cpp:1193).

11. **P1 — Il primo video non imposta durata e fps del documento. Riprodotto.**
    La timeline parte con `frameEnd=120`; l'import aggiorna range e fps solo se `frameEnd<=1`. Una clip sintetica di 300 frame a 30 fps mantiene `frameEnd=120` e `fps=24`: range e timing non rappresentano la clip. Lo stesso ingresso è usato dalle sequenze.
    Intervento: riconoscere esplicitamente il primo video/documento iniziale; decidere come conformare clip con fps differenti.
    [MainWindow.cpp:2881](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2881), [Animation.h:42](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/Animation.h:42).

12. **P2 — Il preset “15 fps” dà 12 campioni/s su timeline 24 fps. Riprodotto.**
    `steppedFrame()` arrotonda il rapporto a un numero intero di frame da mantenere. 24/15 diventa 2, quindi campiona ogni due frame. L'etichetta “fps” inoltre modifica il frame-hold, non gli fps del file esportato.
    Intervento: campionamento con rapporto razionale e label che distingua fps del documento da frequenza dell'effetto.
    [Animation.h:61](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/Animation.h:61), [TimelineWidget.cpp:835](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/TimelineWidget.cpp:835).

13. **P1 — Trasformazioni valide vengono clampate entrando in animazione. Riprodotto.**
    `setParam(TfX,2)` produce 1; `setParam(TfScale,2000)` produce 1000. Canvas e dimensioni numeriche consentono stati oltre questi limiti, ma `paramsAtFrame()` li forza nei range della tabella. Ulteriori disallineamenti: Dot Grid Spacing UI 2–200 contro animazione 2–500; gamma UI arriva a zero mentre la tabella parte da 0,1.
    Intervento: un contratto condiviso di unità e limiti fra modello, UI, keyframe e loader; test dei valori agli estremi.
    [AnimParams.cpp:30](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/AnimParams.cpp:30), [AnimParams.cpp:286](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/AnimParams.cpp:286), [ModePanel.cpp:497](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/ModePanel.cpp:497).

14. **P2 — Editare una trasformazione riscrive anche gli altri campi con valori arrotondati. Riprodotto.**
    `setTransform()` seguito dal percorso `emitTransform()` trasforma rotazione 12,4→12, scala 33,33→33,2226 e aspect 100→99,8291 su sorgente 301×199. Il problema deriva dalla ricostruzione dell'intera trasformazione dai box interi, anche quando si modifica solo un altro campo.
    Intervento: conservare la trasformazione precisa e aggiornare soltanto il campo modificato. Gli arrotondamenti di visualizzazione non devono diventare modifiche del modello.
    [ControlsPanel.cpp:434](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/ControlsPanel.cpp:434).

15. **P2 — I Custom SVG non sono inclusi nei progetti “self-contained”. Verificato nel codice.**
    Le forme salvano solo `svgPath`; il renderer riapre quel percorso. Spostare il progetto su un'altra macchina o rimuovere l'SVG sorgente rompe la composizione. Non è sufficiente incorporare le immagini della Library.
    Intervento: incorporare gli asset SVG oppure gestire dipendenze e relink con un'indicazione esplicita dei file mancanti. Valutare anche i font esterni usati da ASCII/Mosaic.
    [ProjectIO.cpp:123](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/ProjectIO.cpp:123), [DotGridRenderer.cpp:213](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/DotGridRenderer.cpp:213).

**Memoria, velocità e responsività**

16. **P1 — Import video limitato per frame, non per memoria, e troncamento senza avviso. Verificato nel codice.**
    Il default è 1.200 frame interi BGRA in RAM. A 1920×1080 sono circa 9,27 GiB; a 3840×2160 circa 37,08 GiB, prima di cache, anteprime e temporanei. A 30 fps il limite corrisponde a 40 secondi; il loader non segnala che la clip era più lunga. Accetta inoltre un decode parziale purché esista almeno un frame, senza validare l'esito finale del processo.
    Intervento: metadati prima del decode, proxy, cache con budget in byte, decode su richiesta/prefetch, stato “parziale” esplicito. I numeri sono calcoli di memoria, non misure del processo.
    [VideoIO.h:26](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/VideoIO.h:26), [VideoIO.cpp:90](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/VideoIO.cpp:90).

17. **P1 — Import, preparazione playback ed export possono bloccare l'interfaccia. Verificato nel codice.**
    Il preview usa un worker, ma `VideoIO::decode`, export raster/SVG, `renderPreviewCached` del playback e l'encoding finale sono chiamati dal thread UI. Le finestre di avanzamento elaborano eventi fra frame; non rendono cancellabile un render in corso né il `waitForFinished(-1)` dell'encoder.
    Intervento: job in background con snapshot immutabile, progressi e cancellazione reale; mantenere il thread UI dedicato agli eventi. Separare il budget di preview da export evita contesa.
    [MainWindow.cpp:1532](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:1532), [MainWindow.cpp:2634](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2634), [MainWindow.cpp:3267](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:3267).

18. **P2 — MP4 passa da PNG temporanei e ignora errori intermedi. Verificato nel codice.**
    Ogni frame viene compresso PNG, scritto, riletto e decompresso da ffmpeg. `flat.save()` non è verificato; la sequenza PNG esterna conta soltanto i file riusciti e può mostrare “Exported” dopo cancellazione/errori. Ripetere l'export nella stessa cartella sovrascrive i nomi coincidenti senza un piano di conflitti.
    Intervento: pipe di frame raw a ffmpeg, coda limitata, controllo di ogni scrittura, output temporaneo fino al successo e stati distinti per completato/annullato/parziale. Misurare tempo e spazio risparmiati su clip rappresentative.
    [MainWindow.cpp:3198](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:3198), [MainWindow.cpp:3254](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:3254).

19. **P2 — Cache GPU delle texture senza limite di risoluzioni per layer. Verificato nel codice.**
    `m_layerTex` è indicizzata per ID e dimensioni; il cleanup elimina solo gli ID scomparsi. Il corrispondente limite CPU a tre taglie non libera queste texture. Cambiando ripetutamente la risoluzione del rendering di un layer CPU visualizzato su GPU, le vecchie texture possono accumularsi.
    Intervento: LRU con budget in byte e numero massimo di varianti, conservando fast/full recenti. Anche il compositor CPU mantiene un canvas completo per ciascun job/layer prima del blend: limitare la concorrenza in funzione della memoria, non solo dei core.
    [GpuCanvasWidget.cpp:1327](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/GpuCanvasWidget.cpp:1327), [GpuCanvasWidget.cpp:1343](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/GpuCanvasWidget.cpp:1343), [RenderWorker.cpp:465](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/workers/RenderWorker.cpp:465).

20. **P2 — Atlas ASCII cresce quadraticamente ed è conservato senza eviction. Verificato nel codice; picco reale non misurato.**
    `gpuAtlas()` usa una cache statica per font/charset/cella; crea tile 2× più grandi della cella in entrambi gli assi e consente `cellH` fino a 4096. La compensazione della scala può raggiungere dimensioni molto maggiori del cursore UI. Ogni nuova dimensione conserva un'immagine, e l'allocazione/upload non è protetta da un budget o dal limite texture del dispositivo.
    Intervento: budget e LRU; verificare dimensioni texture supportate, fallimento delle allocazioni e fallback. Solo dopo misure valutare un atlas a risoluzione stabile o altra rappresentazione.
    [AsciiRenderer.cpp:261](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/AsciiRenderer.cpp:261), [GpuCanvasWidget.cpp:1166](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/GpuCanvasWidget.cpp:1166).

**UI e UX**

21. **P2 — L'import iniziale è tagliato nella Library. Riprodotto nel layout fuori schermo.**
    A scala 0,960751 il pannello inferiore iniziale resta alto circa 150 pixel, compresa la barra tab. Titolo, margini, spaziatura e CTA dell'empty state richiedono più spazio: “import” resta quasi tutto sotto il bordo sia a 1920×1080 sia a 1280×720. L'empty state è un figlio posizionato manualmente, quindi il suo minimo non corregge quello del contenitore.
    Intervento: dare spazio sufficiente all'empty state quando espanso oppure renderlo compatto; preservare il collasso volontario del pannello. Il primo ingresso nel prodotto deve restare visibile.
    [FilmstripWidget.cpp:315](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/FilmstripWidget.cpp:315), [MainWindow.cpp:315](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:315).

22. **P2 — Scala UI legata al primario all'avvio, non alla finestra. Verificato nel codice e nella prova di resize.**
    Il fattore viene impostato una volta su `1,28 × primaryScreen.availableWidth / 2558`. Riducendo la finestra 1920→1280, resta 0,960751: i pannelli conservano circa 395 pixel ciascuno e il canvas si restringe molto. Non è di per sé necessario scalare i font a ogni resize, ma manca una strategia esplicita per finestre compatte e monitor diversi.
    Intervento: densità/testo leggibili indipendenti dalla larghezza, colonne adattive o comprimibili, dimensionamento sullo schermo di destinazione; test 1366×768 e DPI misti. Evitare un semplice “scala tutto sempre”, che può rendere il testo troppo piccolo.
    [main.cpp:160](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/main.cpp:160), [MainWindow.cpp:392](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:392).

    Migliorie UX collegate: usare W/H per Dimensions invece di X/Y; distinguere “fps documento” da frequenza dell'effetto; indicare documenti modificati nel titolo; pulsanti Save/Discard/Cancel espliciti; ricordare dimensione finestra, splitter e sezioni. Rendere meno prominenti i controlli senza layer selezionato. Sono proposte di prodotto, non crash riprodotti.

**Delivery, ripetizioni e manutenzione**

23. **P1 — CI compila ma non verifica il comportamento o il pacchetto. Verificato nella configurazione.**
    I tre programmi di test sono manuali, senza target CMake/CTest; il workflow non li esegue. Locale Qt 6.11.1 MinGW e CI Qt 6.7.0 MSVC sono ambienti differenti, mentre il renderer usa header QRhi versionati. Non ho dimostrato che la CI attuale fallisca: manca una prova di parità con l'ambiente testato.
    Intervento: target test separati; round-trip progetto, undo media, golden approvati, PNG/SVG e timing; smoke test della cartella distribuita con PATH ripulito. Aggiungere un test GPU di confronto tollerante, non pretendere uguaglianza byte per byte con ogni backend.
    [CMakeLists.txt:1](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/CMakeLists.txt:1), [build.yml:19](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/.github/workflows/build.yml:19), [tests/README.md:3](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/tests/README.md:3).

24. **P2 — Pacchetto non riproducibile e identità della versione disallineata. Verificato nel codice/configurazione.**
    CMake dichiara 1.2.3, il runtime 2.0.0; `app.rc` contiene solo l'icona. Il fetch ffmpeg usa un URL “release” mobile e non verifica un hash/versione. Lo zip contiene solo `build/Release/*`; la build non vi copia esplicitamente LICENSE, THIRD_PARTY_LICENSES e OFL. Questo è un riscontro sul contenuto del packaging, non una conclusione legale. L'associazione `.less` viene riscritta all'avvio anche da una build di sviluppo.
    Intervento: unica versione derivata dal tag, metadati exe, dipendenze identificate e verificate, manifest del pacchetto con testi di licenza, checksum degli artifact, simboli diagnostici separati, verifica in VM pulita. Gestire l'associazione file come scelta di installazione. Valutare firma del pacchetto dopo la stabilizzazione.
    [CMakeLists.txt:3](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/CMakeLists.txt:3), [main.cpp:151](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/main.cpp:151), [fetch-ffmpeg.ps1:13](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/tools/fetch-ffmpeg.ps1:13), [build.yml:54](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/.github/workflows/build.yml:54).

25. **P2 — La duplicazione più costosa riguarda il significato dei dati. Verificato nei percorsi.**
    Playhead/media/visibilità vengono risolti in più funzioni: il bug Ctrl+C dimostra già una divergenza. Matrici e bake sono distribuiti fra MainWindow, RenderWorker e PreviewWidget. Unit/range si ripetono in Params, AnimParams, pannelli, JSON e shader. La selezione dei mode GPU è duplicata fra MainWindow e RenderWorker. MainWindow ha 3.274 righe, ModePanel 2.618, Widgets 2.100: le dimensioni aiutano a localizzare i punti di attrito, ma non sono di per sé bug.
    Intervento: estrazioni piccole con responsabilità precise: snapshot del documento al frame, operazioni undo sul documento, servizio export e descrittori condivisi di unità/range. Evitare una riscrittura generale o un framework di pannelli che aumenti il lavoro prima del lancio. Unificare i contratti fra CPU/GPU, non tentare di usare lo stesso codice di disegno nei due backend.
    [MainWindow.cpp:2575](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/ui/MainWindow.cpp:2575), [RenderWorker.cpp:436](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/workers/RenderWorker.cpp:436), [AnimParams.cpp:12](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/src/core/AnimParams.cpp:12).

**Ottimizzazioni già presenti da conservare**

LUT concatenata per gli adjustment puntuali; parallelizzazione per righe e per layer; cache CPU limitata a tre risoluzioni; riuso delle sorgenti ridotte; skip dei pass GPU durante pan/zoom senza nuovi contenuti; preload delle maschere dither; aggiornamento differito delle miniature. Non conviene sostituirle sulla sola base della lunghezza dei file.

Le misure utili per decidere gli interventi successivi sono: latenza input→anteprima p50/p95, tempo di render completo, throughput export, picco RAM/VRAM, hit rate delle cache e frame tardivi. Usare immagini 1080p/4K, 1/5/20 layer, documenti misti CPU/GPU e clip brevi/lunghe; separare primo render e cache calda. Priorità alle pipe video e ai budget memoria prima delle micro-ottimizzazioni matematiche. Per l'error diffusion, non parallelizzare ingenuamente righe dipendenti: cambierebbe il risultato.

**Ordine di intervento suggerito**

1. Protezione del lavoro: 1–6, 10. Criterio: nessuna sostituzione non confermata, salvataggio atomico, titolo/libreria tracciati, undo completo.
2. Fedeltà: 7–9, 11–14. Criterio: copia/preview/export risolvono lo stesso frame e i keyframe non alterano valori validi.
3. Video e memoria: 16–20. Criterio: budget esplicito, import/export responsivi e annullabili, niente troncamenti o errori nascosti.
4. UI, portabilità e delivery: 15, 21–25. Criterio: import accessibile, dipendenze del progetto gestite, pacchetto identificabile e verificato automaticamente.

Rinvierei nuovi mode e refactoring estesi fino alla chiusura dei primi due gruppi. Autosalvataggio/recovery e preset per-mode sono migliorie utili dopo aver reso affidabile il salvataggio normale.

**Evidenze locali**

Le prove e i risultati intermedi sono in `build/audit-2026-09-05/` (directory esclusa da Git). Il rapporto non certifica l'assenza di ulteriori bug; copre i principali percorsi di stato, rendering, UI, memoria e distribuzione con i limiti indicati sopra.


- [Risultati delle prove](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/audit/prove-2026-09-05.txt)
- [Layout di prova 1280×720](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/audit/ui-1280-2026-09-05.png)
- [Layout di prova 1920×1080](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/audit/ui-1920-2026-09-05.png)
- [Sorgente delle prove mirate](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/build/audit-2026-09-05/audit_probe.cpp)
- [Sorgente delle prove di layout](C:/Users/busel/OneDrive/Desktop/ULTRA_Ditherer/build/audit-2026-09-05/ui_probe.cpp)


Aggiornamento 2026-09-06: gli interventi e i risultati successivi sono documentati in [POINTLESS-correzioni-2026-09-06.md](POINTLESS-correzioni-2026-09-06.md). Questo audit resta la fotografia precedente alle modifiche.
