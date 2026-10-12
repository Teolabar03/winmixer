# obs-winmixer

Plugin per OBS Studio (Windows) che porta l'idea di [winmixer](..) (la versione Python in questa stessa repository) dentro OBS:
un dock "Mixer di Windows" con il volume di sistema e un cursore per ogni
applicazione che sta riproducendo audio, con indicatore di livello in tempo reale.

## Funzionalità

- Voci costruite come il mixer audio nativo di OBS 32: cursore con curva logaritmica in dB,
  indicatori di livello per canale con scala da 0 a -60 dB, picco trattenuto e spia di ingresso.
  Colori, icona del muto e spaziature arrivano dal tema attivo di OBS.
- Cursori e indicatori si adattano alla dimensione del dock, sia in verticale sia in orizzontale.
- Voce "Sistema" per il dispositivo di uscita predefinito, poi una voce per ogni applicazione;
  le sessioni dello stesso processo sono raggruppate, come nel Mixer volume di Windows.
- Le app senza audio in corso sono segnate come "Inattiva".
- Nomi delle app letti dalla descrizione dell'eseguibile (es. "Google Chrome").
- Segue il cambio di dispositivo predefinito e le app che si aprono o si chiudono (ogni 1,5 s).
- Menu con i tre puntini: disposizione verticale e aggiornamento dell'elenco. La disposizione è salvata in
  `%APPDATA%\obs-studio\plugin_config\obs-winmixer\config.json`.
- Testi in italiano e inglese (`data/locale`).

Nota: Core Audio fornisce solo i picchi, non l'RMS. Il segno nero della "magnitudine" è quindi
la media mobile (300 ms) del picco, non l'RMS che mostra OBS per le sue sorgenti.

## Struttura

Ricavata dal reverse engineering (REA + Ghidra) dei plugin installati con OBS
(`win-wasapi.dll`, `frontend-tools.dll`) e basata su
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate):

| File | Ruolo |
|---|---|
| `src/plugin-main.cpp` | esportazioni `obs_module_*`, registrazione del dock con `obs_frontend_add_dock_by_id` |
| `src/audio-sessions.*` | Core Audio: `IAudioEndpointVolume`, `IAudioSessionManager2`, `ISimpleAudioVolume`, `IAudioMeterInformation` |
| `src/mixer-dock.*` | dock, voci (`VolumeControl`) e cursori (`VolumeSlider`) sul modello di OBS 32 |
| `src/volume-meter.*` | indicatore di livello (`VolumeMeter`) adattato da OBS 32 |
| `buildspec.json` | versioni di OBS, obs-deps e Qt usate per compilare |

## Compilazione

Richiede Visual Studio 2022 (C++), Windows SDK 10.0.22621 e CMake 3.28 o successivo
(va bene quello incluso in Visual Studio).

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Al primo avvio CMake scarica nella cartella `.deps` i sorgenti di OBS e le dipendenze precompilate.
Il risultato è `build_x64\RelWithDebInfo\obs-winmixer.dll`.

## Installazione

Copia i file nella cartella dei plugin utente di OBS:

```
C:\ProgramData\obs-studio\plugins\obs-winmixer\bin\64bit\obs-winmixer.dll
C:\ProgramData\obs-studio\plugins\obs-winmixer\data\locale\*.ini
```

Riavvia OBS e attiva il dock da **Docks → Mixer di Windows**.

## Licenza

GPL-2.0 o successiva (obbligatoria per i plugin che usano libobs), vedi `LICENSE`.
