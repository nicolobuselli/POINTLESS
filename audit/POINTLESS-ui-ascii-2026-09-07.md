# Vincoli UI e ASCII Palette — 2026-09-07

Richiesta: rimuovere Unsaved changes; dimensioni invarianti per Library e mode picker; minimo dei pannelli come nello screenshot e massimo contenuto; diagnosticare ASCII lento.

Implementazione UI:
- Eliminata la label dal titolo e i relativi aggiornamenti. Il rilevamento dirty e il dialogo Save/Discard rimangono.
- Library: miniature e pulsante + fissi a 145 unità Ui. Il resize cambia il numero di colonne, non la taglia.
- Mode picker largo 330 unità Ui, altezza originale 42. Nessuna variazione a font e slider.
- Pannelli: minimi 380 sinistra / 410 destra, massimi 560 unità Ui e circa 30% della finestra compatibilmente con i minimi. A scala 1,28 i minimi sono 486/525 pixel, coerenti con lo screenshot. Vincolate anche le quote dello splitter: il solo maximumWidth lasciava avanzare il divisore oltre il contenuto.
- Pannello inferiore iniziale 150 pixel logici come richiesto in precedenza.

ASCII:
Il gate gpuRenderable escludeva tutte le palette, anche nel codice precedente all'audit. I parametri mostrati nello screenshot selezionavano quindi il percorso CPU.
Aggiunto nearest-color OkLab nei due shader ASCII (quadrato e griglie istanziate), usando le otto voci già disponibili nei buffer uniformi. Le palette oltre otto colori, Braille e gli atlas eccessivi mantengono il fallback CPU.
Il test GPU verifica l'effettiva selezione del percorso shader e confronta i colori con il renderer CPU su campioni uniformi, su entrambe le griglie. Non è una dichiarazione di equivalenza pixel per pixel su ogni immagine né una misura degli fps del video dell'utente.
