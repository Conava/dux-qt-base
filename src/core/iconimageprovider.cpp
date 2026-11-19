#include "iconimageprovider.hpp"
#include <algorithm>

#include <private/qiconloader_p.h>
#include <qcolor.h>
#include <qdir.h>
#include <qicon.h>
#include <qlogging.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qsize.h>
#include <qstring.h>

namespace {

// QIcon::fromTheme keeps the last 100 icons, each holding its full size decode of raster theme
// files. The image cache already keeps the scaled result.
QIcon themeIcon(const QString& name) {
	if (QDir::isAbsolutePath(name)) return QIcon(name);
	return QIcon(QIconLoader::instance()->iconEngine(name));
}

} // namespace

QPixmap
IconImageProvider::requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) {
	QString iconName;
	QString fallbackName;
	QString path;

	auto splitIdx = id.indexOf("?path=");
	if (splitIdx != -1) {
		iconName = id.sliced(0, splitIdx);
		path = id.sliced(splitIdx + 6);
		path = QString("/%1/%2").arg(path, iconName.sliced(iconName.lastIndexOf('/') + 1));
	} else {
		splitIdx = id.indexOf("?fallback=");
		if (splitIdx != -1) {
			iconName = id.sliced(0, splitIdx);
			fallbackName = id.sliced(splitIdx + 10);
		} else {
			iconName = id;
		}
	}

	auto icon = themeIcon(iconName);
	if (icon.isNull() && !fallbackName.isEmpty()) icon = themeIcon(fallbackName);
	if (icon.isNull() && !path.isEmpty()) icon = QPixmap(path);

	auto targetSize = requestedSize.isValid() ? requestedSize : QSize(100, 100);
	if (targetSize.width() == 0 || targetSize.height() == 0) targetSize = QSize(2, 2);
	auto pixmap = icon.pixmap(targetSize.width(), targetSize.height());

	if (pixmap.isNull()) {
		qWarning() << "Could not load icon" << id << "at size" << targetSize << "from request";
		pixmap = IconImageProvider::missingPixmap(targetSize);
	}

	if (size != nullptr) *size = pixmap.size();
	return pixmap;
}

QPixmap IconImageProvider::missingPixmap(const QSize& size) {
	auto width = size.width() % 2 == 0 ? size.width() : size.width() + 1;
	auto height = size.height() % 2 == 0 ? size.height() : size.height() + 1;
	width = std::max(width, 2);
	height = std::max(height, 2);

	auto pixmap = QPixmap(width, height);
	pixmap.fill(QColorConstants::Black);
	auto painter = QPainter(&pixmap);

	auto halfWidth = width / 2;
	auto halfHeight = height / 2;
	auto purple = QColor(0xd900d8);
	painter.fillRect(halfWidth, 0, halfWidth, halfHeight, purple);
	painter.fillRect(0, halfHeight, halfWidth, halfHeight, purple);
	return pixmap;
}

QString IconImageProvider::requestString(
    const QString& icon,
    const QString& path,
    const QString& fallback
) {
	auto req = "image://icon/" + icon;

	if (!path.isEmpty()) {
		req += "?path=" + path;
	}

	if (!fallback.isEmpty()) {
		req += "?fallback=" + fallback;
	}

	return req;
}
