# winmixer

Mixer di volume per applicazione per Windows, in una finestra Tkinter compatta e
sempre a portata di mano — come il Mixer volume di Windows, ma standalone e con
i VU-meter in tempo reale.

## Funzionalità

- Slider di volume indipendente per ogni applicazione con una sessione audio attiva.
- VU-meter (livello di picco) in tempo reale per ogni app.
- Layout commutabile tra verticale e orizzontale.
- L'orientamento scelto viene salvato in `config.json` e recuperato al riavvio.

## Plugin per OBS Studio

In [`obs-plugin/`](obs-plugin) c'è la stessa idea come plugin per OBS Studio 32 su Windows:
un dock "Mixer di Windows" con cursori e indicatori di livello identici a quelli del mixer audio di OBS.
Istruzioni di compilazione e installazione nel suo [README](obs-plugin/README.md).
Il plugin è distribuito con licenza GPL-2.0 (obbligatoria per i plugin che usano libobs).

## Avvio

```bash
pip install pycaw comtypes
python mixer.py
```

Richiede Windows (usa le API Core Audio via `pycaw`).

È incluso anche `mixer.spec`, per compilarlo in un eseguibile standalone con
[PyInstaller](https://pyinstaller.org/):

```bash
pip install pyinstaller
pyinstaller mixer.spec
```
