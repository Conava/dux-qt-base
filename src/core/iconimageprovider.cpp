#include "iconimageprovider.hpp"
#include <algorithm>

#include <private/qiconloader_p.h>
#include <qchar.h>
#include <qcolor.h>
#include <qdir.h>
#include <qicon.h>
#include <qlogging.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qsize.h>
#include <qstring.h>
#include <qstringview.h>

// "rev" only changes the string to get past Qt Quick's pixmap cache. It is read as the last
// parameter only; a name or path ending in "?rev=" or "&rev=" and digits is the one exception.
IconImageProvider::Request IconImageProvider::parseRequest(const QString& id) {
	auto rest = QStringView(id);

	const auto revIdx = std::max(rest.lastIndexOf(u"?rev="), rest.lastIndexOf(u"&rev="));
	if (revIdx != -1) {
		const auto revision = rest.sliced(revIdx + 5);
		const auto isDigit = [](QChar c) { return c >= u'0' && c <= u'9'; };
		if (!revision.isEmpty() && std::ranges::all_of(revision, isDigit)) rest = rest.first(revIdx);
	}

	auto splitIdx = rest.indexOf(u"?path=");
	if (splitIdx != -1) {
		return {
		    .icon = rest.first(splitIdx).toString(),
		    .path = rest.sliced(splitIdx + 6).toString(),
		    .fallback = QString(),
		    .hasPath = true,
		};
	}

	splitIdx = rest.indexOf(u"?fallback=");
	if (splitIdx != -1) {
		return {
		    .icon = rest.first(splitIdx).toString(),
		    .path = QString(),
		    .fallback = rest.sliced(splitIdx + 10).toString(),
		    .hasPath = false,
		};
	}

	return {.icon = rest.toString(), .path = QString(), .fallback = QString(), .hasPath = false};
}

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
	const auto request = IconImageProvider::parseRequest(id);

	QString path;
	if (request.hasPath) {
		const auto fileName = request.icon.sliced(request.icon.lastIndexOf('/') + 1);
		path = QString("/%1/%2").arg(request.path, fileName);
	}

	auto icon = themeIcon(request.icon);
	if (icon.isNull() && !request.fallback.isEmpty()) icon = themeIcon(request.fallback);
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
