/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/liquid_glass/liquid_glass_widget.h"

#include "base/debug_log.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/gl_shader.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"

#include <QtCore/QFile>
#include <QtGui/QLinearGradient>
#include <QtGui/QOpenGLContext>
#include <QtGui/QPainter>
#include <QtGui/QtEvents>

#include <QOpenGLShaderProgram>

namespace Ui {
namespace {

constexpr auto kFragmentShaderName = "liquid_glass.frag";

// The shader itself has no #version / precision header so that it stays
// a clean port of the AGSL original. The header required by Telegram's
// GL context (core profile 2.1 / ES 2.0, see Ui::GL::FragmentShader in
// lib_ui/ui/gl/gl_shader.cpp) is prepended here instead.
[[nodiscard]] QString ShaderVersionHeader() {
	const auto context = QOpenGLContext::currentContext();
	Assert(context != nullptr);

	const auto es = (context->format().renderableType()
		== QSurfaceFormat::OpenGLES);
	return es
		? QString("#version 100\nprecision highp float;\n")
		: QString("#version 120\n");
}

[[nodiscard]] QString ReadFragmentShaderSource() {
	// The .frag file lives next to this .cpp in the source tree. Try the
	// Qt resource system first (in case it gets embedded in a .qrc), then
	// fall back to reading it from the sources directly.
	auto dir = QString::fromUtf8(__FILE__);
	dir.replace('\\', '/');
	dir = dir.left(dir.lastIndexOf('/') + 1);

	const auto candidates = {
		QString(":/ui/effects/liquid_glass/") + kFragmentShaderName,
		QString(":/gl/") + kFragmentShaderName,
		dir + kFragmentShaderName,
	};
	for (const auto &path : candidates) {
		auto file = QFile(path);
		if (file.open(QIODevice::ReadOnly)) {
			return QString::fromUtf8(file.readAll());
		}
	}
	LOG(("Liquid Glass Error: Could not read %1."
		).arg(kFragmentShaderName));
	return QString();
}

} // namespace

LiquidGlassWidget::LiquidGlassWidget(QWidget *parent, QWidget *background)
: QOpenGLWidget(parent)
, _background(background) {
	setAttribute(Qt::WA_TranslucentBackground);
	setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

LiquidGlassWidget::~LiquidGlassWidget() {
	makeCurrent();
	freeResources();
	doneCurrent();
}

void LiquidGlassWidget::setRadius(float radius) {
	setRadius(radius, radius, radius, radius);
}

void LiquidGlassWidget::setRadius(
		float topLeft,
		float topRight,
		float bottomRight,
		float bottomLeft) {
	if (_radiusTopLeft == topLeft
		&& _radiusTopRight == topRight
		&& _radiusBottomRight == bottomRight
		&& _radiusBottomLeft == bottomLeft) {
		return;
	}
	_radiusTopLeft = topLeft;
	_radiusTopRight = topRight;
	_radiusBottomRight = bottomRight;
	_radiusBottomLeft = bottomLeft;
	update();
}

void LiquidGlassWidget::setThickness(float thickness) {
	if (_thickness == thickness) {
		return;
	}
	_thickness = thickness;
	update();
}

void LiquidGlassWidget::setIntensity(float intensity) {
	if (_intensity == intensity) {
		return;
	}
	_intensity = intensity;
	update();
}

void LiquidGlassWidget::setRefractIndex(float index) {
	if (_refractIndex == index) {
		return;
	}
	_refractIndex = index;
	update();
}

void LiquidGlassWidget::setForegroundColor(QColor color) {
	if (_foregroundColor == color) {
		return;
	}
	_foregroundColor = color;
	update();
}

void LiquidGlassWidget::setBlurRadius(int radius) {
	if (_blurRadius == radius) {
		return;
	}
	_blurRadius = radius;
	refreshBackdrop();
}

void LiquidGlassWidget::refreshBackdrop() {
	_backdropDirty = true;
	update();
}

void LiquidGlassWidget::initializeGL() {
	initializeOpenGLFunctions();

	if (_contextDestroyed) {
		QObject::disconnect(base::take(_contextDestroyed));
	}
	const auto raw = context();
	_contextDestroyed = QObject::connect(
		raw,
		&QOpenGLContext::aboutToBeDestroyed,
		[=] {
			makeCurrent();
			freeResources();
			doneCurrent();
		});

	const auto fragmentSource = ReadFragmentShaderSource();
	if (fragmentSource.isEmpty()) {
		return;
	}
	_program = std::make_unique<QOpenGLShaderProgram>();
	Ui::GL::LinkProgram(
		_program.get(),
		Ui::GL::VertexShader({}),
		ShaderVersionHeader() + fragmentSource);

	// Full-viewport quad, two triangles in normalized device coordinates.
	static const float coords[] = {
		-1.f, -1.f,
		 1.f, -1.f,
		 1.f,  1.f,
		-1.f, -1.f,
		 1.f,  1.f,
		-1.f,  1.f,
	};
	_vertexBuffer.emplace();
	_vertexBuffer->create();
	_vertexBuffer->bind();
	_vertexBuffer->allocate(coords, sizeof(coords));
}

void LiquidGlassWidget::resizeGL(int w, int h) {
	_backdropDirty = true;
}

void LiquidGlassWidget::moveEvent(QMoveEvent *e) {
	QOpenGLWidget::moveEvent(e);
	refreshBackdrop();
}

void LiquidGlassWidget::showEvent(QShowEvent *e) {
	QOpenGLWidget::showEvent(e);
	refreshBackdrop();
}

void LiquidGlassWidget::ensureBackdrop() {
	if (!_backdropDirty) {
		return;
	}
	_backdropDirty = false;

	// Debug safe mode: do not grab or touch the background widget, use a
	// synthetic vertical gradient as the backdrop instead.
	if (size().isEmpty()) {
		_backdrop = QImage();
		return;
	}
	auto blurred = QImage(
		size() * devicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	{
		auto gradient = QLinearGradient(0., 0., 0., blurred.height());
		gradient.setColorAt(0., QColor(70, 90, 140));
		gradient.setColorAt(1., QColor(20, 25, 40));
		auto p = QPainter(&blurred);
		p.fillRect(blurred.rect(), gradient);
	}
	if constexpr (Ui::GL::kSwizzleRedBlue) {
		blurred = std::move(blurred).rgbSwapped();
	}
	// QImage rows start at the top, the shader flips Y expecting a
	// bottom-row-first (OpenGL style) texture, so flip while uploading.
	_backdrop = std::move(blurred).mirrored();
	uploadBackdrop();
}

void LiquidGlassWidget::uploadBackdrop() {
	if (_backdrop.isNull()) {
		return;
	}
	if (!_backdropTexture) {
		glGenTextures(1, &_backdropTexture);
		glBindTexture(GL_TEXTURE_2D, _backdropTexture);
		glTexParameteri(
			GL_TEXTURE_2D,
			GL_TEXTURE_MIN_FILTER,
			GL_LINEAR);
		glTexParameteri(
			GL_TEXTURE_2D,
			GL_TEXTURE_MAG_FILTER,
			GL_LINEAR);
		glTexParameteri(
			GL_TEXTURE_2D,
			GL_TEXTURE_WRAP_S,
			GL_CLAMP_TO_EDGE);
		glTexParameteri(
			GL_TEXTURE_2D,
			GL_TEXTURE_WRAP_T,
			GL_CLAMP_TO_EDGE);
	} else {
		glBindTexture(GL_TEXTURE_2D, _backdropTexture);
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, _backdrop.bytesPerLine() / 4);
	if (_backdropTextureSize != _backdrop.size()) {
		_backdropTextureSize = _backdrop.size();
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			Ui::GL::kFormatRGBA,
			_backdrop.width(),
			_backdrop.height(),
			0,
			Ui::GL::kFormatRGBA,
			GL_UNSIGNED_BYTE,
			_backdrop.constBits());
	} else {
		glTexSubImage2D(
			GL_TEXTURE_2D,
			0,
			0,
			0,
			_backdrop.width(),
			_backdrop.height(),
			Ui::GL::kFormatRGBA,
			GL_UNSIGNED_BYTE,
			_backdrop.constBits());
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

QVector4D LiquidGlassWidget::shaderRadius(float pixelRatio) const {
	// The shader picks r.xy for p.x > 0 (right half) and r.x for
	// p.y > 0 (bottom half, top-left origin), so the uniform order is
	// (rightBottom, rightTop, leftBottom, leftTop) - same as the
	// Android implementation passes it.
	auto topLeft = _radiusTopLeft;
	auto topRight = _radiusTopRight;
	auto bottomRight = _radiusBottomRight;
	auto bottomLeft = _radiusBottomLeft;

	const auto w = float(width());
	const auto h = float(height());
	const auto fit = [](float &a, float &b, float side) {
		if (side > 0 && a + b > side) {
			const auto k = a / (a + b);
			a = side * k;
			b = side * (1.f - k);
		}
	};
	fit(topLeft, bottomLeft, h);
	fit(topRight, bottomRight, h);
	fit(topLeft, topRight, w);
	fit(bottomLeft, bottomRight, w);

	return QVector4D(
		bottomRight * pixelRatio,
		topRight * pixelRatio,
		bottomLeft * pixelRatio,
		topLeft * pixelRatio);
}

void LiquidGlassWidget::paintGL() {
	glClearColor(0.f, 0.f, 0.f, 0.f);
	glClear(GL_COLOR_BUFFER_BIT);

	if (!_program || !_vertexBuffer || size().isEmpty()) {
		return;
	}
	ensureBackdrop();
	if (!_backdropTexture) {
		return;
	}

	const auto ratio = float(devicePixelRatio());
	const auto w = float(width()) * ratio;
	const auto h = float(height()) * ratio;

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _backdropTexture);

	_program->bind();
	_program->setUniformValue("img", GLint(0));
	_program->setUniformValue("resolution", QVector2D(w, h));
	_program->setUniformValue("center", QVector2D(w / 2.f, h / 2.f));
	_program->setUniformValue("size", QVector2D(w / 2.f, h / 2.f));
	_program->setUniformValue("radius", shaderRadius(ratio));
	_program->setUniformValue(
		"thickness",
		GLfloat(std::max(_thickness, 1.f) * ratio));
	_program->setUniformValue(
		"refract_index",
		GLfloat(std::max(_refractIndex, 1.f)));
	_program->setUniformValue("refract_intensity", GLfloat(_intensity));
	const auto alpha = float(_foregroundColor.alphaF());
	_program->setUniformValue(
		"foreground_color_premultiplied",
		QVector4D(
			float(_foregroundColor.redF()) * alpha,
			float(_foregroundColor.greenF()) * alpha,
			float(_foregroundColor.blueF()) * alpha,
			alpha));

	_vertexBuffer->bind();
	const auto position = _program->attributeLocation("position");
	glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		nullptr);
	glEnableVertexAttribArray(position);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(position);

	_program->release();
}

void LiquidGlassWidget::freeResources() {
	if (_contextDestroyed) {
		QObject::disconnect(base::take(_contextDestroyed));
	}
	if (_backdropTexture) {
		if (QOpenGLContext::currentContext()) {
			glDeleteTextures(1, &_backdropTexture);
		}
		_backdropTexture = 0;
		_backdropTextureSize = QSize();
	}
	_vertexBuffer.reset();
	_program = nullptr;
	_backdropDirty = true;
}

} // namespace Ui
