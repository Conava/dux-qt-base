#pragma once

#include <qpixmap.h>
#include <qquickimageprovider.h>
#include <qsize.h>
#include <qstring.h>

class IconImageProvider: public QQuickImageProvider {
public:
	explicit IconImageProvider(): QQuickImageProvider(QQuickImageProvider::Pixmap) {}

	QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;

	static QPixmap missingPixmap(const QSize& size);

	struct Request {
		QString icon;
		QString path;
		QString fallback;
		// An empty "?path=" still names a directory: the root.
		bool hasPath = false;
	};

	// Splits the part of a request string that follows "image://icon/".
	static Request parseRequest(const QString& id);

	static QString requestString(
	    const QString& icon,
	    const QString& path = QString(),
	    const QString& fallback = QString()
	);
};
