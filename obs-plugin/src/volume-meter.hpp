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

#include <QColor>
#include <QFont>
#include <QSize>
#include <QWidget>

#include <cstdint>

/* Indicatore di livello modellato sul VolumeMeter di OBS 32 (frontend/components/VolumeMeter.cpp).
 * Il nome della classe e le Q_PROPERTY sono gli stessi di proposito: cosi' le regole
 * "VolumeMeter { qproperty-... }" del tema attivo di OBS lo colorano come il mixer nativo. */
class VolumeMeter : public QWidget {
	Q_OBJECT
	Q_PROPERTY(QColor backgroundNominalColor MEMBER backgroundNominalColor DESIGNABLE true)
	Q_PROPERTY(QColor backgroundWarningColor MEMBER backgroundWarningColor DESIGNABLE true)
	Q_PROPERTY(QColor backgroundErrorColor MEMBER backgroundErrorColor DESIGNABLE true)
	Q_PROPERTY(QColor foregroundNominalColor MEMBER foregroundNominalColor DESIGNABLE true)
	Q_PROPERTY(QColor foregroundWarningColor MEMBER foregroundWarningColor DESIGNABLE true)
	Q_PROPERTY(QColor foregroundErrorColor MEMBER foregroundErrorColor DESIGNABLE true)
	Q_PROPERTY(QColor backgroundNominalColorDisabled MEMBER backgroundNominalColorDisabled DESIGNABLE true)
	Q_PROPERTY(QColor backgroundWarningColorDisabled MEMBER backgroundWarningColorDisabled DESIGNABLE true)
	Q_PROPERTY(QColor backgroundErrorColorDisabled MEMBER backgroundErrorColorDisabled DESIGNABLE true)
	Q_PROPERTY(QColor foregroundNominalColorDisabled MEMBER foregroundNominalColorDisabled DESIGNABLE true)
	Q_PROPERTY(QColor foregroundWarningColorDisabled MEMBER foregroundWarningColorDisabled DESIGNABLE true)
	Q_PROPERTY(QColor foregroundErrorColorDisabled MEMBER foregroundErrorColorDisabled DESIGNABLE true)
	Q_PROPERTY(QColor clipColor MEMBER clipColor DESIGNABLE true)
	Q_PROPERTY(QColor magnitudeColor MEMBER magnitudeColor DESIGNABLE true)
	Q_PROPERTY(QColor majorTickColor MEMBER majorTickColor DESIGNABLE true)
	Q_PROPERTY(QColor minorTickColor MEMBER minorTickColor DESIGNABLE true)

public:
	static constexpr int kMaxChannels = 8;

	explicit VolumeMeter(bool vertical, QWidget *parent = nullptr);

	/* Picchi lineari (0..1) per canale, letti da IAudioMeterInformation */
	void setPeaks(const float *peaks, int channels);
	void setMuted(bool muted);
	void setUseDisabledColors(bool disabled);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	void setChannelCount(int channels);
	void resetLevels();
	void calculateBallistics(uint64_t ts, double secondsSinceLastRedraw);
	void paintTicks(QPainter &painter);
	QColor inputPeakColor(float peakHold) const;
	QRect barRect() const;

	bool vertical_;
	bool muted_ = false;
	bool useDisabledColors_ = false;
	bool clipping_ = false;
	int channels_ = 2;
	int meterThickness_ = 6;
	QSize tickTokenSize_;

	float currentPeak_[kMaxChannels];
	float displayPeak_[kMaxChannels];
	float displayPeakHold_[kMaxChannels];
	float displayMagnitude_[kMaxChannels];
	float displayInputPeakHold_[kMaxChannels];
	uint64_t peakHoldTime_[kMaxChannels];
	uint64_t inputPeakHoldTime_[kMaxChannels];
	uint64_t lastRedrawTime_ = 0;

	/* Valori predefiniti di OBS, usati solo se il tema non li imposta */
	QColor backgroundNominalColor{0x26, 0x7f, 0x26};
	QColor backgroundWarningColor{0x7f, 0x7f, 0x26};
	QColor backgroundErrorColor{0x7f, 0x26, 0x26};
	QColor foregroundNominalColor{0x4c, 0xff, 0x4c};
	QColor foregroundWarningColor{0xff, 0xff, 0x4c};
	QColor foregroundErrorColor{0xff, 0x4c, 0x4c};
	QColor backgroundNominalColorDisabled{90, 90, 90};
	QColor backgroundWarningColorDisabled{117, 117, 117};
	QColor backgroundErrorColorDisabled{65, 65, 65};
	QColor foregroundNominalColorDisabled{163, 163, 163};
	QColor foregroundWarningColorDisabled{217, 217, 217};
	QColor foregroundErrorColorDisabled{113, 113, 113};
	QColor clipColor{0xff, 0xff, 0xff};
	QColor magnitudeColor{0x00, 0x00, 0x00};
	QColor majorTickColor{0x00, 0x00, 0x00};
	QColor minorTickColor{0x32, 0x32, 0x32};
};
