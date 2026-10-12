/*
obs-winmixer
Copyright (C) 2026 Matteo La Barbera <matteo.labarbera@digitalsupport-tech.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include "audio-sessions.hpp"

#include <QFrame>
#include <QMap>
#include <QSlider>

class QAction;
class QBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QTimer;
class QToolButton;
class VolumeMeter;

/* Cursore che salta al punto cliccato e ignora la rotella se non ha il focus,
 * come AbsoluteSlider/SliderIgnoreScroll di OBS */
class VolumeSlider : public QSlider {
	Q_OBJECT

public:
	VolumeSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

private:
	int valueAt(QMouseEvent *event);
	bool dragging_ = false;
};

/* Una voce del mixer, con la stessa struttura e gli stessi nomi del VolumeControl di OBS 32
 * (#volLabel, #volMeterFrame, .mixer-name, .mixer-category, .btn-mute) per ereditarne lo stile. */
class VolumeControl : public QFrame {
	Q_OBJECT

public:
	VolumeControl(const QString &name, bool vertical, QWidget *parent = nullptr);

	void setName(const QString &name);
	void setCategory(const QString &text, const QString &stateClass);
	/* Volume in dB (-inf = silenzio) */
	void setState(float db, bool muted);
	void setPeaks(const float *peaks, int channels);

signals:
	void volumeChanged(float db);
	void muteToggled(bool muted);

private:
	void showDb(float db);
	void showMuted(bool muted);

	bool vertical_;
	QString name_;
	QString stateClass_;
	QLabel *categoryLabel_;
	QLabel *nameLabel_;
	QLabel *volumeLabel_;
	QPushButton *muteButton_;
	VolumeSlider *slider_;
	VolumeMeter *meter_;
};

class WinMixerDock : public QWidget {
	Q_OBJECT

public:
	explicit WinMixerDock(QWidget *parent = nullptr);
	~WinMixerDock() override;

	/* Ferma i timer e rilascia gli oggetti COM prima della chiusura di OBS */
	void shutdown();

private slots:
	void refreshSessions();
	void updateMeters();
	void setVertical(bool vertical);

private:
	void buildLayout();
	void clearControls();
	const AppAudio *findApp(DWORD pid) const;
	void loadConfig();
	void saveConfig() const;

	AudioSessions sessions_;
	bool vertical_ = false;

	QLabel *device_ = nullptr;
	QToolButton *optionsButton_ = nullptr;
	QAction *verticalAction_ = nullptr;
	QScrollArea *scroll_ = nullptr;
	QBoxLayout *controlsLayout_ = nullptr;
	QLabel *empty_ = nullptr;
	VolumeControl *master_ = nullptr;
	QMap<DWORD, VolumeControl *> controls_;

	QTimer *meterTimer_ = nullptr;
	QTimer *sessionTimer_ = nullptr;
};
