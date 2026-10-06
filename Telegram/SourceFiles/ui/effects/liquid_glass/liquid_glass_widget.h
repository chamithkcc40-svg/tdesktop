/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QPointer>
#include <QtGui/QColor>
#include <QtGui/QOpenGLFunctions>

#include <QOpenGLBuffer>
#include <QOpenGLWidget>

#include <memory>
#include <optional>

class QOpenGLShaderProgram;

namespace Ui {

// Qt OpenGL widget that draws a blurred snapshot of a background widget
// refracted through the "liquid glass" fragment shader
// (ui/effects/liquid_glass/liquid_glass.frag).
//
// The widget grabs the pixels of the `background` widget that lie behind
// its own geometry, blurs them on the CPU (Images::BlurLargeImage) and
// uploads the result as the `img` texture sampled by the shader. The
// rounded-rectangle glass shape always matches the widget's own rect.
class LiquidGlassWidget final
	: public QOpenGLWidget
	, protected QOpenGLFunctions {

public:
	LiquidGlassWidget(QWidget *parent, QWidget *background);
	~LiquidGlassWidget();

	// Same corner radius for all four corners, in logical pixels.
	void setRadius(float radius);

	// Per-corner radii, in logical pixels.
	void setRadius(
		float topLeft,
		float topRight,
		float bottomRight,
		float bottomLeft);

	// Thickness of the refracting glass border, in logical pixels.
	void setThickness(float thickness);

	// Strength of the refraction displacement, [0..1].
	void setIntensity(float intensity);

	// Index of refraction of the glass (1.0 means no refraction).
	void setRefractIndex(float index);

	// Tint drawn over the refracted backdrop (straight alpha,
	// premultiplied before upload). Transparent by default.
	void setForegroundColor(QColor color);

	// Radius of the CPU blur applied to the grabbed backdrop.
	void setBlurRadius(int radius);

	// Re-grab the backdrop from the background widget. Call this when
	// the content behind the glass has changed or the widget was moved.
	void refreshBackdrop();

protected:
	void initializeGL() override;
	void resizeGL(int w, int h) override;
	void paintGL() override;

	void moveEvent(QMoveEvent *e) override;
	void showEvent(QShowEvent *e) override;

private:
	void ensureBackdrop();
	void uploadBackdrop();
	void freeResources();

	[[nodiscard]] QVector4D shaderRadius(float pixelRatio) const;

	QPointer<QWidget> _background;

	std::unique_ptr<QOpenGLShaderProgram> _program;
	std::optional<QOpenGLBuffer> _vertexBuffer;
	QMetaObject::Connection _contextDestroyed;

	GLuint _backdropTexture = 0;
	QSize _backdropTextureSize;
	QImage _backdrop;
	bool _backdropDirty = true;

	float _radiusTopLeft = 0.;
	float _radiusTopRight = 0.;
	float _radiusBottomRight = 0.;
	float _radiusBottomLeft = 0.;
	float _thickness = 11.;
	float _intensity = 0.75;
	float _refractIndex = 1.5;
	QColor _foregroundColor = QColor(255, 255, 255, 0);
	int _blurRadius = 24;

};

} // namespace Ui
