# neo2-llkh

*alternativer Treiber mittels Low-Level Keyboard Hook*

(*Read this in [English](README.en.md)*)

Dieser Treiber unterstützt folgende Tastaturlayouts:
* [Neo2](https://www.neo-layout.org)
* [Aus der Neo-Welt](http://www.adnw.de)
* [AdNWzjßf](http://adnw.de/index.php?n=Main.AdNWzj%c3%9ff)
* [bone](https://web.archive.org/web/20180721192908/http://wiki.neo-layout.org/wiki/Bone)
* [KOY](http://adnw.de/index.php?n=Main.SeitlicheNachbaranschl%c3%a4ge)
* [VOU](https://maximilian-schillinger.de/vou-layout.html) (mein persönliches Layout auf Basis von KOY)
* [QWERTZ](https://de.wikipedia.org/wiki/QWERTZ-Tastaturbelegung)
* Andere Layouts können über eine Konfigurationsdatei definiert werden.

## Funktionsumfang
* Alle 6 Ebenen (auf den Ebenen 5 und 6 funktionieren nicht alle Zeichen)
* Die meisten toten Tasten werden unterstützt: ``^ ` ´ ̧ ̌ ~ ° ̇  `` (teilweise muss die tote Taste vor, teilweise nach dem Buchstaben gedrückt werden)
* Level2-Lock: Ebene 2 kann eingerastet werden (beide Shift-Tasten gleichzeitig)
* Echtes CapsLock: Alle Buchstaben groß schreiben. Zahlen, Punkt und Komma bleiben unverändert. (Beide Shift-Tasten gleichzeitig)
* Level4-Lock: Ebene 4 kann eingerastet werden (beide Mod4-Tasten gleichzeitig)
* Der rechte Ebene3-Modifier kann statt auf die ö- auf die ä- oder Enter-Taste gelegt werden
* Die linke Ebene4-Modifier kann auf die Tabulator-Taste gelegt werden
* Wenn gewünscht, gilt für Shortcuts (mit Strg, Alt und/oder Win) das QWERTZ-Layout
* Die linke Strg- und Alt-Taste können getauscht werden
* Alternativ können die linke Strg-, Alt- und Windows-Taste getauscht werden
* Die CapsLock-Taste kann in eine zusätzliche Escape-Taste verwandelt werden (wenn sie alleine angeschlagen wird)
* Der rechte Ebene3-Modifier kann in eine zusätzliche Enter-Taste verwandelt werden (wenn er alleine angeschlagen wird)
* Der linke Ebene4-Modifier kann in eine zusätzliche Tab-Taste verwandelt werden (wenn er alleine angeschlagen wird)

### Was nicht funktioniert

* Diese Tasten sollten eigentlich tote Tasten sein, geben aber direkt die entsprechenden Symbole aus: `̷  ¨ ˝`
* Die Ebenen 2-6 für den Nummernblock funktionieren nur teilweise.

### Bekannte Fehler

* ~~Ein Benutzer hat berichtet, dass es ab und zu passiert, dass die Strg-Taste "virtuell" zu klemmen scheint. Wenn das passiert, kann jede weitere Eingabe eine Tastenkombination mit Strg auslösen. Vorsicht: Hier besteht die Gefahr eines Datenverlustes (wenn z.B. ein ungespeichertes Dokument ungewollt geschlossen wird).~~ Dieser Fehler kann mit Commit [aca671f183](https://github.com/MaxGyver83/neo2-llkh/tree/aca671f1830ebe1f9e74b82c6d0cba77067daa61) vom 8.10.2019 reproduziert werden. Er tritt in der aktuellen Version jedoch nicht mehr auf.
* Eine Benutzerin hat berichtet, dass die Scollfunktion ihres Touchpads ausfällt, wenn sie Enter (AltGr+V) oder Escape (AltGr+Y) auf Ebene 4 im Neo-Layout betätigt.

## Selbst kompilieren
Um diesen Treiber aus den Quellen zu installieren, klone dieses Projekt (`git clone https://github.com/MaxGyver83/neo2-llkh.git`) oder lade es als zip herunter und entpacke es. Führe dann `make` im `src`-Ordner aus. Dafür müssen make und gcc installiert sein. Wenn du diese Programme noch nicht hast, könntest du z.B. [MinGW](https://sourceforge.net/projects/mingw/) installieren.

## Verwendung
Starte einfach die selbst kompilierte `neo-llkh.exe` aus dem `src`-Ordner oder lade `neo-llkh.exe` und `settings.ini` von https://github.com/MaxGyver83/neo2-llkh/releases runter. Standardmäßig wird das Neo2-Layout geladen.

Über das Icon (![appicon](src/appicon.ico)) in der Taskleiste kannst du den Treiber wieder beenden.

## Einstellungen

Alle Einstellungen werden in der mitgelieferten `settings.ini` gemacht. Es wird immer die ini-Datei aus dem gleichen Ordner wie die `neo-llkh.exe` verwendet. Um eine Funktion zu aktivieren, setze den entsprechenden Wert auf `1`. `0` bedeutet deaktiviert.

### Layout
Wenn du ein anderes Layout verwenden möchtest, ändere den `layout`-Eintrag in der `settings.ini`, zum Beispiel:

`layout=adnw`

Folgende Layout-Parameter werden erkannt: `neo`, `adnw`, `adnwzjf` (=AdNWzjßf), `bone`, `koy`, `kou`, `vou`, `qwertz`.

### Benutzerdefiniertes Layout
Ein eigenes Layout kann wie folgt definiert werden, hier am Beispiel von *Cry*:

`customLayout=jluaqwbdgyzßcrieomntshvxüäöpf,.k`

Dabei entsprechen die ersten 12 Zeichen den Tasten `Q` bis `+`, die nächsten 11 Zeichen `A` bis `Ä` und die letzten 11 Zeichen `Y` bis `-`.

### Einrasten von Ebene 2
Das Einrasten von Ebene 2 (beide Shift-Tasten gleichzeitig) wird unterstützt, muss aber explizit aktiviert werden. Dafür muss der Wert von `shiftLockEnabled` in der `settings.ini` auf `1` gesetzt werden:

`shiftLockEnabled=1`

### Echtes CapsLock
CapsLock (beide Shift-Tasten gleichzeitig) wird unterstützt, muss aber explizit aktiviert werden. Dafür muss der Wert von `capsLockEnabled` in der `settings.ini` auf `1` gesetzt werden:

`capsLockEnabled=1`

### Einrasten von Ebene 4
Das Einrasten von Ebene 4 (beide Mod4-Tasten \[`<>` + `AltGr`\] gleichzeitig) wird unterstützt, muss aber explizit aktiviert werden. Dafür muss der Wert von `level4LockEnabled` in der `settings.ini` auf `1` gesetzt werden:

`level4LockEnabled=1`

Wenn Level2- und Level4-Lock aktiv sind, wirkt nur der Level4-Lock.

### Symmetrische Level3-Modifier
Wenn du den rechten Ebene3-Modifier auf die `Ä`- statt auf die `#`-Taste legen willst, setze den Wert von `symmetricalLevel3Modifiers` auf `1`:

`symmetricalLevel3Modifiers=1`

### Return-Taste als rechten Level3-Modifier verwenden
Bei US-Tastaturen fehlt in der mittleren Reihe die #-Taste. Wenn du den rechten Ebene3-Modifier auf die Return-Taste legen willst, setze den Wert von `returnKeyAsMod3R` auf `1`:

`returnKeyAsMod3R=1`

Diese Einstellung kann gut mit `mod3RAsReturn` kombiniert werden.

### Tab-Taste als linken Level4-Modifier verwenden
Bei US-Tastaturen fehlt die <-Taste (unten links). Wenn du den linken Ebene4-Modifier auf die Tab-Taste legen willst, setze den Wert von `tabKeyAsMod4L` auf `1`:

`tabKeyAsMod4L=1`

Diese Einstellung kann gut mit `mod4LAsTab` kombiniert werden.

### QWERTZ-Layout für Shortcuts
Außerdem gibt es einen Modus, in dem bei Shortcuts immer das QWERTZ-Layout gilt. Das heißt, immer wenn Strg, die linke Alt-Taste oder eine Windows-Taste gedrückt ist, wird unabhängig vom eingestellten Layout QWERTZ verwendet. Somit können Strg-c, Strg-v, Strg-s usw. einfach mit der linken Hand betätigt werden. Um diesen Modus zu aktivieren, muss `qwertzForShortcuts` in der `settings.ini` auf `1` gesetzt werden:

`qwertzForShortcuts=1`

### Linke Alt- und Strg-Taste vertauschen
Für Mac-Freunde, die es gewohnt sind, Shortcuts mit dem linken Daumen auszuführen.

`swapLeftCtrlAndLeftAlt=1`

Achtung: Bitte den Hinweis am Ende dieser Datei beachten!

### Linke Alt-, Strg- und Win-Taste vertauschen
Wie die vorige Option, allerdings wird hier zusätzlich die linke Windows-Taste versetzt.
Damit ergibt sich die Reihenfolge: Win, Alt, Ctrl (auf Standardtastaturen)

`swapLeftCtrlLeftAltAndLeftWin=1`

Achtung: Bitte den Hinweis am Ende dieser Datei beachten!

### Ebenen 5 und 6
Achtung: Experimentell! Nicht alle Tasten funktionieren, obwohl ihnen Symbole zugewiesen wurden. Möglicherweise können nicht alle Programme alle Zeichen darstellen.

`supportLevels5and6=1`

### CapsLock-Taste als Escape verwenden
Für vim-Freunde: Wenn die CapsLock-Taste einzeln angeschlagen wird, sendet sie ein Escape. In Kombination mit anderen Tasten ist sie der linke Ebene3-Modifier.

`capsLockAsEscape=1`

### Rechten Level3-Modifier als Return verwenden
Wenn die rechte Ebene3-Modifier-Taste einzeln angeschlagen wird, sendet sie ein Return. In Kombination mit anderen Tasten ist sie der rechte Ebene3-Modifier.

`mod3RAsReturn=1`

### Linken Level4-Modifier als Tab verwenden
Wenn die linke Ebene4-Modifier-Taste einzeln angeschlagen wird, sendet sie ein Tab. In Kombination mit anderen Tasten ist sie der linke Ebene4-Modifier.

`mod4LAsTab=1`

### Windows plus Leerschlag als bypass-toggle-shortcut benutzen
Es kann <kbd>Windows</kbd>+<kbd>Leerschlag</kbd> statt <kbd>Schift</kbd>+<kbd>Pause</kbd> benutzt werden um zwischen bypass und normaler Mode umzuschalten.

`winSpaceForToggle=1`

### Debug-Ausgabe in einem separaten Fenster
`neo-llkh.exe` gibt standardmäßig Debug-Information aus, die aber nicht sichtbar sind, wenn der Treiber per Doppelklick oder im cmd-Fenster gestartet wird. Um die Ausgabe zu sehen, muss sie entweder (in *cmd*) in eine Datei umgeleitet werden (`.\neo-llkh.exe > log.txt`) oder (sofern vorhanden) in *Git Bash* gestartet werden. Oder man setzt folgenden Parameter auf 1. Dann erscheint ein separates Debug-Fenster, auch beim Start per Doppelklick.

`debugWindow=1`

Hinweis: Mit den Schließen des Debug-Fensters wird auch der Treiber beendet!

### Einstellungen als Parameter

Wenn der Treiber über die Kommandozeile gestartet wird, können alle Einstellungen auch als Parameter übergeben werden. Beispiel:

`neo-llkh layout=koy supportLevels5and6=1 qwertzForShortcuts=1`

Das Layout kann auch direkt angeben werden:

`neo-llkh adnw`

Die Einstellungen aus der `settings.ini` werden trotzdem berücksichtigt. Wenn ein Parameter sowohl in der `settings.ini` als auch in der Kommandozeile gesetzt wird, hat letzterer Vorrang.


### Vertauschen von Strg und Alt (und ggf. Win)

Wenn du die Option `swapLeftCtrlAndLeftAlt` oder `swapLeftCtrlLeftAltAndLeftWin` verwenden möchtest, solltest du den Treiber unbedingt mit Admin-Rechten starten (Rechtsklick auf `neo2-llkh.exe` → Als Administrator ausführen)! Andernfalls besteht folgende Problematik: In Systemprogrammen (z.B. `regedit`) wird der Treiber ignoriert, es gilt also z.B. QWERTZ. Wenn du mit Alt-Tab zu einem "normalen" Programm wechselst, wird beim Wechsel die Alt-Taste auf Strg umgemappt und dadurch nicht gelöst (weil sie ja zum Zeitpunkt des Lösens bereits eine Strg-Taste ist). Somit bleit Alt "virtuell" gedrückt und der nächste Tastendruck löst einen ungewollten Shortcut mit unerwarteten Folgen aus.
