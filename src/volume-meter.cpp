/*
obs-winmixer
Copyright (C) 2026 Matteo La Barbera <matteo.labarbera@digitalsupport-tech.com>

Portions adapted from OBS Studio frontend/components/VolumeMeter.cpp,
Copyright (C) OBS Studio contributors, GPL-2.0-or-later.

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

#include "volume-meter.hpp"

#include <util/platform.h>

#include <QEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

/* Stessi valori di OBS 32.2.2 */
constexpr float MINIMUM_LEVEL = -60.0f;
constexpr float WARNING_LEVEL = -20.0f;
constexpr float ERROR_LEVEL = -9.0f;
constexpr float CLIP_LEVEL = 0.0f;
constexpr float MINIMUM_INPUT_LEVEL = -50.0f;
constexpr double PEAK_DECAY_RATE = 11.76;          // 20 dB / 1,7 s
constexpr double MAGNITUDE_INTEGRATION_TIME = 0.3; // 99% in 300 ms
constexpr double PEAK_HOLD_DURATION = 20.0;
constexpr double INPUT_PEAK_HOLD_DURATION = 1.0;

constexpr int INDICATOR_THICKNESS = 3;
constexpr int CLIP_FLASH_DURATION_MS = 1000;
constexpr int TICK_SIZE = 2;
constexpr int TICK_DB_INTERVAL = 6;
constexpr const char *TICK_LABEL_TOKEN = "-88";
constexpr float TICK_LABEL_HEIGHT_SCALE_FACTOR = 0.8f;

constexpr float NEG_INF = -std::numeric_limits<float>::infinity();

int toInt(float number)
{
	constexpr int min = std::numeric_limits<int>::min();
	constexpr int max = std::numeric_limits<int>::max();
	if (number >= static_cast<float>(max))
		return max;
	if (number < static_cast<float>(min))
		return min;
	return static_cast<int>(number);
}

float toDb(float amplitude)
{
	return amplitude > 0.0f ? 20.0f * std::log10(amplitude) : NEG_INF;
}

} // namespace

VolumeMeter::VolumeMeter(bool vertical, QWidget *parent) : QWidget(parent), vertical_(vertical)
{
	setAttribute(Qt::WA_OpaquePaintEvent, true);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setFocusPolicy(Qt::NoFocus);

	if (vertical_)
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
	else
		setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

	tickTokenSize_ = fontMetrics().size(Qt::TextSingleLine, TICK_LABEL_TOKEN);
	meterThickness_ = std::clamp(22 / channels_, 3, 6);
	resetLevels();
}

void VolumeMeter::resetLevels()
{
	for (int i = 0; i < kMaxChannels; ++i) {
		currentPeak_[i] = NEG_INF;
		displayPeak_[i] = NEG_INF;
		displayPeakHold_[i] = NEG_INF;
		displayMagnitude_[i] = NEG_INF;
		displayInputPeakHold_[i] = NEG_INF;
		peakHoldTime_[i] = 0;
		inputPeakHoldTime_[i] = 0;
	}
}

void VolumeMeter::setChannelCount(int channels)
{
	channels = std::clamp(channels, 1, kMaxChannels);
	if (channels == channels_)
		return;

	channels_ = channels;
	meterThickness_ = std::clamp(22 / channels_, 3, 6);
	resetLevels();
	updateGeometry();
}

void VolumeMeter::setPeaks(const float *peaks, int channels)
{
	if (channels > 0)
		setChannelCount(channels);

	for (int i = 0; i < channels_; ++i)
		currentPeak_[i] = i < channels ? toDb(peaks[i]) : NEG_INF;

	/* Come OBS: l'attacco viene registrato subito, il decadimento al ridisegno */
	calculateBallistics(os_gettime_ns(), 0.0);
	update();
}

void VolumeMeter::setMuted(bool muted)
{
	if (muted_ == muted)
		return;
	muted_ = muted;
	update();
}

void VolumeMeter::setUseDisabledColors(bool disabled)
{
	if (useDisabledColors_ == disabled)
		return;
	useDisabledColors_ = disabled;
	update();
}

void VolumeMeter::calculateBallistics(uint64_t ts, double dt)
{
	for (int i = 0; i < channels_; ++i) {
		const float peak = currentPeak_[i];

		if (peak >= displayPeak_[i] || std::isnan(displayPeak_[i])) {
			displayPeak_[i] = peak;
		} else {
			float decay = static_cast<float>(PEAK_DECAY_RATE * dt);
			displayPeak_[i] = std::clamp(displayPeak_[i] - decay, std::min(peak, 0.0f), 0.0f);
		}

		if (peak >= displayPeakHold_[i] || !std::isfinite(displayPeakHold_[i])) {
			displayPeakHold_[i] = peak;
			peakHoldTime_[i] = ts;
		} else if ((ts - peakHoldTime_[i]) * 1e-9 > PEAK_HOLD_DURATION) {
			displayPeakHold_[i] = peak;
			peakHoldTime_[i] = ts;
		}

		if (peak >= displayInputPeakHold_[i] || !std::isfinite(displayInputPeakHold_[i])) {
			displayInputPeakHold_[i] = peak;
			inputPeakHoldTime_[i] = ts;
		} else if ((ts - inputPeakHoldTime_[i]) * 1e-9 > INPUT_PEAK_HOLD_DURATION) {
			displayInputPeakHold_[i] = peak;
			inputPeakHoldTime_[i] = ts;
		}

		/* Core Audio fornisce solo i picchi, non l'RMS: l'indicatore di magnitudine
		 * e' l'integrazione VU (300 ms) del picco, con la stessa formula di OBS */
		if (!std::isfinite(displayMagnitude_[i])) {
			displayMagnitude_[i] = std::isfinite(peak) ? std::max(peak, MINIMUM_LEVEL) : peak;
		} else {
			float target = std::isfinite(peak) ? peak : MINIMUM_LEVEL;
			float attack = static_cast<float>((target - displayMagnitude_[i]) *
							  (dt / MAGNITUDE_INTEGRATION_TIME) * 0.99);
			displayMagnitude_[i] = std::clamp(displayMagnitude_[i] + attack, MINIMUM_LEVEL, 0.0f);
		}
	}
}

QColor VolumeMeter::inputPeakColor(float peakHold) const
{
	if (peakHold < MINIMUM_INPUT_LEVEL)
		return backgroundNominalColor;
	if (peakHold < WARNING_LEVEL)
		return foregroundNominalColor;
	if (peakHold < ERROR_LEVEL)
		return foregroundWarningColor;
	if (peakHold < CLIP_LEVEL)
		return foregroundErrorColor;
	return clipColor;
}

QRect VolumeMeter::barRect() const
{
	QRect bar = rect();
	if (vertical_)
		bar.setWidth(channels_ * (meterThickness_ + 1) - 1);
	else
		bar.setHeight(channels_ * (meterThickness_ + 1) - 1);
	return bar;
}

void VolumeMeter::paintTicks(QPainter &painter)
{
	painter.setFont(font());
	QFontMetrics metrics(font());
	painter.setPen(majorTickColor);

	if (vertical_) {
		const int x = channels_ * (meterThickness_ + 1) - 1;
		const int height = rect().height() - (INDICATOR_THICKNESS + 3);
		const qreal scale = height / MINIMUM_LEVEL;
		for (int i = 0; i >= MINIMUM_LEVEL; i -= TICK_DB_INTERVAL) {
			int position = int(i * scale);
			QString str = QString::number(i);
			if (i == 0)
				painter.drawText(x + 10, position + metrics.capHeight(), str);
			else
				painter.drawText(x + 8, position + (metrics.capHeight() / 2), str);
			painter.drawLine(x, position, x + TICK_SIZE, position);
		}
	} else {
		const int x = INDICATOR_THICKNESS + 3;
		const int y = channels_ * (meterThickness_ + 1) - 1;
		const int width = rect().width() - (INDICATOR_THICKNESS + 3);
		const qreal scale = width / MINIMUM_LEVEL;
		for (int i = 0; i >= MINIMUM_LEVEL; i -= TICK_DB_INTERVAL) {
			int position = int(x + width - (i * scale) - 1);
			QString str = QString::number(i);
			QRect bounds = metrics.boundingRect(str);
			int pos = i == 0 ? position - bounds.width() : std::max(0, position - bounds.width() / 2);
			painter.drawText(pos, y + 4 + metrics.capHeight(), str);
			painter.drawLine(position, y, position, y + TICK_SIZE);
		}
	}
}

void VolumeMeter::paintEvent(QPaintEvent *)
{
	const uint64_t ts = os_gettime_ns();
	const double dt = lastRedrawTime_ ? (ts - lastRedrawTime_) * 1e-9 : 0.0;
	calculateBallistics(ts, dt);

	QPainter painter(this);
	painter.fillRect(rect(), palette().color(QPalette::Window));
	paintTicks(painter);

	const bool disabled = muted_ || useDisabledColors_;
	const QColor bgNominal = disabled ? backgroundNominalColorDisabled : backgroundNominalColor;
	const QColor bgWarning = disabled ? backgroundWarningColorDisabled : backgroundWarningColor;
	const QColor bgError = disabled ? backgroundErrorColorDisabled : backgroundErrorColor;
	const QColor nominal = disabled ? foregroundNominalColorDisabled : foregroundNominalColor;
	const QColor warning = disabled ? foregroundWarningColorDisabled : foregroundWarningColor;
	const QColor error = disabled ? foregroundErrorColorDisabled : foregroundErrorColor;

	const int meterStart = INDICATOR_THICKNESS + 2;
	const int meterLength = vertical_ ? rect().height() - (INDICATOR_THICKNESS + 2)
					  : rect().width() - (INDICATOR_THICKNESS + 2);
	const qreal scale = meterLength / MINIMUM_LEVEL;

	const int warningPosition = meterLength - toInt(WARNING_LEVEL * scale);
	const int errorPosition = meterLength - toInt(ERROR_LEVEL * scale);
	const int clipPosition = meterLength - toInt(CLIP_LEVEL * scale);
	const int nominalLength = warningPosition;
	const int warningLength = nominalLength + (errorPosition - warningPosition);

	for (int channel = 0; channel < channels_; ++channel) {
		const int offset = channel * (meterThickness_ + 1);

		auto fill = [&](int length, const QColor &color) {
			if (vertical_)
				painter.fillRect(offset, meterLength, meterThickness_, -length, color);
			else
				painter.fillRect(meterStart, offset, length, meterThickness_, color);
		};

		/* Sfondo a tre zone (verde, giallo, rosso) */
		fill(meterLength, bgError);
		fill(warningLength, bgWarning);
		fill(nominalLength, bgNominal);

		int peakPosition = meterLength - toInt(displayPeak_[channel] * scale);
		const int peakHoldPosition = meterLength - toInt(displayPeakHold_[channel] * scale);
		const int magnitudePosition = meterLength - toInt(displayMagnitude_[channel] * scale);

		if (clipping_)
			peakPosition = meterLength;

		if (peakPosition >= clipPosition) {
			if (!clipping_) {
				QTimer::singleShot(CLIP_FLASH_DURATION_MS, this, [this]() { clipping_ = false; });
				clipping_ = true;
			}
			fill(meterLength, error);
		} else {
			if (peakPosition > errorPosition)
				fill(std::min(peakPosition, meterLength), error);
			if (peakPosition > warningPosition)
				fill(std::min(peakPosition, warningLength), warning);
			if (peakPosition > meterStart)
				fill(std::min(peakPosition, nominalLength), nominal);
		}

		/* Indicatore del picco trattenuto (20 s) */
		QColor holdColor = nominal;
		if (peakHoldPosition >= errorPosition)
			holdColor = error;
		else if (peakHoldPosition >= warningPosition)
			holdColor = warning;
		if (peakHoldPosition - 3 > 0) {
			if (vertical_)
				painter.fillRect(offset, meterLength - peakHoldPosition - 3, meterThickness_, 3,
						 holdColor);
			else
				painter.fillRect(meterStart + peakHoldPosition - 3, offset, 3, meterThickness_,
						 holdColor);
		}

		/* Indicatore della magnitudine */
		if (magnitudePosition - 3 >= 0) {
			if (vertical_)
				painter.fillRect(offset, meterLength - magnitudePosition - 3, meterThickness_, 3,
						 magnitudeColor);
			else
				painter.fillRect(meterStart + magnitudePosition - 3, offset, 3, meterThickness_,
						 magnitudeColor);
		}

		/* Spia di ingresso */
		if (vertical_)
			painter.fillRect(offset, rect().height(), meterThickness_, -INDICATOR_THICKNESS,
					 inputPeakColor(displayInputPeakHold_[channel]));
		else
			painter.fillRect(0, offset, INDICATOR_THICKNESS, meterThickness_,
					 inputPeakColor(displayInputPeakHold_[channel]));
	}

	lastRedrawTime_ = ts;
}

void VolumeMeter::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		tickTokenSize_ = fontMetrics().size(Qt::TextSingleLine, TICK_LABEL_TOKEN);
		updateGeometry();
	}
	QWidget::changeEvent(event);
}

QSize VolumeMeter::minimumSizeHint() const
{
	return sizeHint();
}

QSize VolumeMeter::sizeHint() const
{
	const QRect bar = barRect();
	const int labelTotal = std::abs(static_cast<int>(MINIMUM_LEVEL) / TICK_DB_INTERVAL) + 1;
	if (vertical_) {
		int width = bar.width() + tickTokenSize_.width() + TICK_SIZE + 10;
		int height = int(labelTotal * (tickTokenSize_.height() * TICK_LABEL_HEIGHT_SCALE_FACTOR)) +
			     INDICATOR_THICKNESS;
		return QSize(width, height);
	}
	int width = labelTotal * tickTokenSize_.width() + INDICATOR_THICKNESS;
	int height = bar.height() + tickTokenSize_.height();
	return QSize(width, height);
}
