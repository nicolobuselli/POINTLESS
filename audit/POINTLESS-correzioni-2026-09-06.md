# POINTLESS — correzioni dell’audit

Aggiornamento del 6 settembre 2026. Riferimento: audit/POINTLESS-audit-2026-09-05.md.
Le righe e le prove dell’audit originale descrivono lo stato precedente alle correzioni.

Sono stati applicati interventi su tutti i 25 punti, seguendo l’ordine: protezione del lavoro, fedeltà del risultato, video/memoria, UI e delivery. I limiti delle soluzioni e delle verifiche sono indicati sotto; non è una certificazione dell’assenza di altri difetti.

| Punto | Intervento applicato |
|---|---|
| 1 | Apertura: progetto validato prima della sostituzione; Save/Discard/Cancel protegge le modifiche correnti. |
| 2 | Dirty state include titolo e libreria; indicatore di modifiche nella barra della finestra. |
| 3 | Undo/redo ripristina media, layer, titolo e contatore ID; cronologia limitata per numero di passi e immagini condivise. |
| 4 | Salvataggio atomico con QSaveFile; encoding, scrittura e commit verificati. Un errore preserva il file precedente. |
| 5 | Formato .less v2: video e sequenze vengono salvati integralmente come frame PNG con fps. Nessun layer video viene scartato. Lettura compatibile con v1. |
| 6 | Controlli su versione, struttura, tipi, numeri, dimensioni, riferimenti, keyframe, immagini e budget. Un documento non valido non modifica quello aperto. |
| 7 | SVG: i blend non-Normal appiattiscono la composizione in un’immagine incorporata, conservando il risultato. Gli export Normal mantengono i percorsi vettoriali. Errori e annullamento preservano la destinazione. |
| 8 | Copia, preview, PNG/JPEG, SVG, sequenza e MP4 usano lo stesso snapshot al frame, con animazione e visibilità dei gruppi. |
| 9 | Ogni richiesta di rendering ha una revisione; i risultati di richieste superate non vengono pubblicati. |
| 10 | Il worker invalida le richieste e attende le operazioni attive prima di distruggere le proprie cache. |
| 11 | Il primo video posizionato imposta durata e fps corretti; duplicarlo non reimposta una timeline già modificata. |
| 12 | Campionamento razionale: 15 campioni su 24 frame producono davvero 15 campioni. Playback basato sul tempo trascorso anziché sul numero di eventi timer. |
| 13 | Corretti gli intervalli delle posizioni e della scala animate, della spaziatura Dot Grid e della gamma interessata. |
| 14 | Ogni box modifica solo il proprio campo; il modello conserva i valori frazionari degli altri campi. Dimensions usa W/H. |
| 15 | SVG e pattern personalizzati incorporati nel progetto; ripristino in una cache temporanea con nomi derivati dal contenuto. Persistono anche trim e offset dei gruppi. |
| 16 | Import video: rimosso il taglio implicito a 1200 frame; errori ffmpeg, timeout, frame incompleti e superamento del budget interrompono interamente l’import. |
| 17 | Import immagini/video/sequenze, apertura/salvataggio, copia, prerender e export in background, con dialogo modale e annullamento cooperativo. |
| 18 | MP4 tramite pipe di frame raw; rimossi API e percorso PNG intermedi inutilizzati. Destinazione sostituita dopo il successo; sequenze con conferma sovrascrittura e conteggio distinto di completamento/interruzione. |
| 19 | Cache CPU limitata in byte; compositing a piccoli gruppi rilasciati subito. Texture GPU con LRU e massimo tre varianti per layer. Identità sorgenti basata su cacheKey, non su indirizzi riutilizzabili. Cache CPU/GPU distinte nel contratto delle voci. |
| 20 | Atlas ASCII LRU di 64 MiB, singolo atlas limitato a 32 MiB/4096 px; limite texture del dispositivo verificato. Fallback CPU per atlas troppo grandi o errori delle risorse controllate. Anche la cache delle coperture dei glifi è limitata. |
| 21 | Pannello inferiore iniziale più alto: CTA import visibile; resta possibile il collasso volontario. |
| 22 | Densità limitata tra 0,72 e 1,0; colonne adattive 1:4:1; minimi compatti; geometria e splitter ricordati. Toolbar timeline adattiva e picker che mostra anche la frequenza nativa del video; Effect fps e tooltip distinguono campionamento da fps del documento. |
| 23 | Target CMake/CTest, preset Profile e test GPU/CPU. CI allineata a Qt 6.11.1/MSVC 2022, con test e smoke test del pacchetto. |
| 24 | Versione 2.0.0 unica da CMake e metadati Windows. ffmpeg 8.1.1 fissato e verificato SHA-256. Pacchetto con file runtime selezionati, licenze, manifest/checksum; simboli separati. Associazione .less esplicita, non riscritta all’avvio. |
| 25 | Estratti snapshot al frame e gestione comune dei lavori in background; centralizzate eleggibilità GPU e ripristino undo. Rimosso il vecchio encoder duplicato. Nessuna riscrittura generale dei pannelli. |

**Verifiche eseguite**

- Build Profile Qt 6.11.1 / MinGW 13.1 completata.
- CTest: document_state_check, export_frame_check, audit_regression_check, nearest_glyph_check e gpu_render_check. Cinque test passati nell’esecuzione completa finale.
- Copertura mirata: cancel in apertura, file precedente preservato dopo errore, dirty titolo/media, undo della rimozione sorgente, progetto con video/asset senza file originale, campi JSON invalidi, SVG Multiply e destinazione non scrivibile, trasformazioni frazionarie, primo video 300 frame/30 fps, risultati di render superati, transizione cache GPU→CPU, MP4 round-trip/budget/cancel.
- Test nativo D3D11 su AMD Radeon: pixel Multiply GPU e CPU 64,32,96; errore massimo RGB 0 nel campione verificato. Non è una copertura di tutti i mode/backend.
- Confronto prima/dopo: 14 PNG di render_golden identici byte per byte. Verifica di conservazione del rendering, non un’approvazione estetica indipendente degli output.
- Oracle ASCII: 159.951 confronti, zero discrepanze.
- Layout verificato tramite finestre Qt di prova a 1920×1080, 1366×768, 1280×720 e 960×540; import visibile, testo Library a capo e colonne compatte. Timeline su due righe nelle finestre strette, con spazio aggiuntivo all’apertura del tab.
- Pacchetto locale preparato in build/audit-package-2026-09-06: avvio, risorse e render CPU verificati con PATH privo delle directory Qt/compilatore. Manifest SHA-256 verificato su 26 file, zero discrepanze; licenze presenti e hash del ffmpeg locale verificato.

**Limiti espliciti**

- Video ancora interamente decodificati in RAM. Il budget complessivo delle sorgenti è 512 MiB: oltre questo limite l’import viene rifiutato senza troncare il contenuto. Per clip lunghe serve un successivo decoder su richiesta/proxy; non è stato introdotto qui.
- File progetto massimo 768 MiB. Un progetto v2 richiede questa build o una successiva; le build precedenti non lo leggono. I file v1 restano apribili.
- L’annullamento dei lavori di rendering attende la fine del frame/layer già in elaborazione. Le operazioni ffmpeg verificano l’annullamento durante la comunicazione con il processo.
- I budget delle cache non limitano la RAM/VRAM totale: sorgenti, cronologia, frame corrente e risorse GPU attive hanno costi propri. Le texture del frame corrente non vengono eliminate per rispettare il budget delle varianti inattive.
- Per preservare blend complessi un SVG può contenere una composizione raster, quindi non è interamente vettoriale.
- CI remota MSVC, download completo su una macchina nuova e VM pulita non sono stati eseguiti in questa sessione. È stato verificato il pacchetto locale e predisposto il percorso automatico; nessuna release è stata pubblicata.
- Non sono stati misurati p50/p95 della latenza UI né throughput su un catalogo 4K/20 layer. L’eliminazione dei PNG intermedi e i limiti di cache sono verificati nel percorso implementato; non viene dichiarato un fattore universale di accelerazione.
- Firma del pacchetto, autosalvataggio/recovery, preset e refactoring più estesi restano proposte future dell’audit, non difetti riprodotti che richiedessero nuove funzionalità per questa correzione.

**Riferimenti**

- Build pronta: build/Desktop_Qt_6_11_1_MinGW_64_bit-Profile/POINTLESS.exe.
- Log di build e prove: build/audit-2026-09-05/.
- Immagini: audit/ui-1280-fixed-2026-09-06.png e audit/ui-1920-fixed-2026-09-06.png.
- Test e comandi: tests/README.md; preset: CMakePresets.json.
- Dipendenza fissata: [release ffmpeg 8.1.1](https://github.com/GyanD/codexffmpeg/releases/tag/8.1.1).
- Kit CI: [configurazioni Windows Qt 6.11](https://doc.qt.io/qt-6/windows.html).


Aggiornamento 2026-09-07: le modifiche di layout dell’audit sono state annullate su richiesta dell’utente; i video ora usano una cache temporanea mappata. Il rapporto POINTLESS-regressioni-2026-09-07.md prevale sulle precedenti descrizioni di UI e limite video.
