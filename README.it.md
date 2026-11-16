# T1 Unlocker

[English](README.md) | Italiano

App GUI per sbloccare il **Samsung Portable SSD T1** (cifrato hardware) su Linux, senza Windows.
L'interfaccia dell'app è in inglese.

## Descrizione

T1 Unlocker rileva automaticamente il disco T1 collegato (Samsung, VID `04e8`), invia il
comando vendor di sblocco via libusb (protocollo "USBC" su USB Bulk-Only, password come
payload 512 byte), esegue il relink del dispositivo USB e monta la partizione sbloccata
via udisks2 (D-Bus, best-effort). Un polling periodico aggiorna lo stato della UI:
`NoDevice`, `Locked`, `Unlocked`, `Transient`, errori di permessi o del device.

La password non viene mai salvata su disco o loggata, e il buffer viene azzerato dalla
memoria dopo l'uso (`explicit_bzero`).

## Requisiti

- Linux x86_64, glibc ≥ 2.31 (baseline ubuntu:20.04; l'AppImage porta con sé le librerie GTK)
- Librerie di sistema non incluse nell'AppImage: `libusb-1.0` e `libudev`
  (presenti su tutte le distribuzioni moderne; `libudev` deve combaciare col
  daemon udev dell'host, mentre `libgudev` viaggia nel bundle accoppiata alla
  glib dell'AppImage)
- **udisks2 attivo** (servizio `udisks2` in esecuzione): serve per il mount automatico
  post-sblocco. Il mount è best-effort: senza udisks2 lo sblocco funziona, ma la
  partizione va montata a mano.
- Accesso al device USB: vedi [Permessi USB](#permessi-usb)
- Samsung Portable SSD T1: PID `8001` (unità di riferimento), `61f1`/`61f2` (T1 classici)

## Build

### Script (consigliato): build completa + AppImage

```bash
packaging/build-appimage.sh
# → T1Unlocker-0.1.0-x86_64.AppImage
```

Lo script fa il build release nel container Docker `t1-unlocker-build` (ubuntu:20.04),
assembla l'AppDir (binario, desktop entry, appdata, icone), scarica `linuxdeploy` e il
runtime AppImage e produce l'output finale con bundling GTK3. Nota: `libudev` **non**
viene inclusa nel bundle perché deve combaciare col daemon udev dell'host; `libgudev`
invece viaggia nel bundle, accoppiata alla glib dell'AppImage.

### Manuale (nel container)

```bash
docker build -t t1-unlocker-build packaging/
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src t1-unlocker-build \
    bash -c "cd /src && meson setup build --buildtype=release && ninja -C build"
./build/src/t1-unlocker
```

### Test

```bash
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src t1-unlocker-build \
    bash -c "cd /src && meson test -C build"
```

17 test unitari GTest senza hardware: protocollo con `UsbIo` mockato (payload, risposta,
CSW leniente, zero-pad, relink), error-path di `usb_io`, classificazione stato da firme
`BlockDevInfo` mockate.

## Permessi USB

L'accesso raw al device USB richiede permessi adeguati. La via consigliata è una regola
udev con tag `uaccess`, che concede l'accesso all'utente della sessione desktop attiva:

```bash
sudo tee /etc/udev/rules.d/99-t1-unlocker.rules > /dev/null <<'EOF'
SUBSYSTEM=="usb", ATTRS{idVendor}=="04e8", ATTRS{idProduct}=="8001", MODE="0660", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Dopo l'installazione della regola, scollegare e ricollegare il disco. In alternativa al
`uaccess` (distribuzioni senza systemd-logind o sessioni non attive), impostare `GROUP`
al gruppo degli utenti ammessi (es. `plugdev`) nella stessa regola.

## Checklist test manuali (§9 della spec)

Da eseguire su disco T1 reale (colonna **Esito** da compilare durante la sessione di test).

| # | Test | Esito atteso | Esito |
|---|------|--------------|-------|
| 1 | Sblocco normale + mount automatico (udisks2) | Sblocco riuscito e partizione montata | **PASS** (2026-11-16, dopo 4 fix: reset USB post-relink, close pre-attesa, case D-Bus UDisks2, Variant (a{sv})) |
| 2 | Auto-lock al power cycle (scollega/ricollega) | Stato `Locked` al ri-collegamento | **PASS** (2026-11-16) |
| 3 | Password errata | Dialog di errore + avviso nel log + bottone riabilitato dopo relink | **PASS** (2026-11-16; relink ha ri-presentato il LUN bloccato, verificato con lsblk) |
| 4 | Estrazione a metà operazione | Stato `NoDevice` pulito, nessun crash | **PASS** (2026-11-16; dialog "Errore di comunicazione USB", app integra e riutilizzabile subito dopo) |
| 5 | Ri-collegamento dopo sblocco e uso | Nuovo ciclo normale | **PASS** (2026-11-16; più cicli completi nella sessione) |
| 6 | Permessi negati (rimozione temporanea del tag uaccess) | Stato `PermissionError` con hint udev/uaccess | **Coperta da unit test** (classificazione PermissionError) e doppia review della propagazione LIBUSB_ERROR_ACCESS in `open_first`; test manuale richiede modifica temporanea a regole udev di sistema, rinviata |
| 7 | Doppio avvio | Risollevamento della finestra esistente | **PASS** (2026-11-16) |
| 8 | Timing relink → Unlocked | Aggiornamento stato entro 1-2 poll (2-4 s) | **PASS** (2026-11-16; 1-2 s dichiarati dall'utente) |
| 9 | Resume-from-suspend | Stato UI coerente al risveglio | **N/A** su questa macchina (nessuna opzione di sospensione); il rischio coperto (stato stantio dopo sparizione del disco) è dimostrato dalle voci 2 e 4: lo stato è ricalcolato da udev a ogni poll da 2 s, mai ricordato |
| 10 | Smoke AppImage su distro diversa da quella di build | Avvio senza crash | **RINVIATA** (2026-11-16: nessuna seconda macchina x86_64 sottomano; qualsiasi distro con glibc ≥ 2.31, anche solo avvio senza disco) |

## Limiti noti

- **Auto-lock al power cycle**: il firmware ri-blocca il disco a ogni scollegamento o
  spegnimento. È il comportamento hardware previsto: dopo ogni ri-collegamento serve un
  nuovo sblocco.
- **Tentativi falliti**: il numero massimo di tentativi di sblocco tollerato dal firmware
  è sconosciuto. Inserire la password con attenzione; in caso di fallimenti ripetuti,
  scollegare e ricollegare il disco prima di riprovare.
- **Password in chiaro sul bus USB**: il protocollo Samsung trasmette la password in
  chiaro sul bus USB: chi ha accesso fisico al bus può sniffarla. È un rischio residuo
  intrinseco del firmware, non mitigabile lato app (§8 della spec). L'app non salva mai
  la password su disco né la logga, e la azzera dalla memoria dopo l'uso.

## Licenza

Rilasciato sotto [licenza MIT](LICENSE).

## Crediti

- [pssdunlock](https://github.com/redchenjs/pssdunlock) di redchenjs: fonte dei dettagli
  del protocollo (comandi vendor di sblocco e relink). Il repo upstream non dichiara una
  licenza esplicita: T1 Unlocker non ne copia il codice, usa solo la conoscenza del
  protocollo.
