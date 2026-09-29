# Correzioni delle regressioni segnalate il 7 settembre 2026

## UI: ripristino del comportamento precedente
Su richiesta esplicita dell'utente sono annullati il clamp della scala, le colonne con stretch 1:4:1, la persistenza della geometria introdotta nell'audit, la timeline su due righe e l'espansione automatica del pannello al cambio tab. Ripristinati scala originale, colonne 0:1:0 con dimensioni iniziali scalate 410/1738/410, avvio massimizzato e altezza minima originale. Nessuna nuova misura per font, slider o pulsanti del mode picker.
Il pannello inferiore parte a 150 pixel logici; si ignorano le vecchie dimensioni salvate dall'audit. Ridotti solo gli spazi della Library vuota per far entrare frase e import nel pannello compatto.
La scritta Unsaved changes ha sfondo trasparente sulla barra. Il dialogo propone Save e Discard; X ed Esc annullano.

## Import video
Il limite di 512 MiB era applicato alla somma dei fotogrammi decompressi e respingeva file piccoli. VideoIO ora scrive i pixel nativi in una cache temporanea mappata, senza tenere una copia allocata di ciascun fotogramma. Le QImage mantengono in vita il file; le modifiche ai pixel producono copie separate. La riapertura dei video dai progetti usa la stessa cache. Nessuna riduzione di risoluzione o di numero di fotogrammi.
Il salvataggio non somma i pixel mappati al budget RAM delle sorgenti; resta il limite effettivo del formato JSON di 768 MiB, controllato anche durante l'accumulo dei frame codificati. Serve spazio temporaneo su disco: la cache contiene pixel decompressi, non il video compresso. Le pagine mappate sono gestite dal sistema operativo; non si dichiara un limite alla RAM totale del processo.

## Verifica
Build Profile ottimizzata e cinque test CTest. Nuova regressione: MP4 Full HD di 7.706 byte, 70 frame, 580.608.000 byte decompressi (oltre 512 MiB): import completo, salvataggio e riapertura, ultimo frame corretto, distacco delle copie modificate e pulizia delle cache dopo rilascio/annullamento. Test anche per Save/Discard senza Cancel e annullamento del dialogo.
Il clip specifico dell'utente non è stato fornito: la prova riproduce la causa del rifiuto, non certifica quel file.
La build corretta è separata nella sottocartella corrected del Profile per non terminare l'app aperta con modifiche non salvate.
