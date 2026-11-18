#pragma once

#include <qbytearray.h>
#include <qdir.h>
#include <qlist.h>
#include <qstring.h>
#include <qtypes.h>

#include "scan.hpp"

class TestToolSupport;

namespace qs::core {

class QmlToolingSupport {
public:
	static bool updateTooling(const QDir& configRoot, bool keepMirror);
	static QString
	updateMirror(const QDir& configRoot, const QmlScanner& scanner, bool copyDocuments);
	static bool writeTooling(const QDir& configRoot, const QmlScanner& scanner);

private:
	struct Document {
		QString source;
		QString target;
		bool fresh;
	};

	static QString getQmllsConfig();
	static QString qmlTypesDir();
	enum class LockResult : quint8 {
		Acquired,
		Contended,
		Failed,
	};

	static LockResult lockTooling(bool oneShot = false);
	static bool updateQmllsConfig(const QDir& configRoot, bool create);
	static bool mirrorDir(
	    const QmlScanner& scanner,
	    const QDir& source,
	    const QDir& target,
	    QList<Document>* documents
	);
	static bool mirrorEntry(
	    const QmlScanner& scanner,
	    const QString& path,
	    const QString& target,
	    QList<Document>* documents,
	    bool fresh
	);
	static bool copyDocuments(const QList<Document>& documents);
	static bool copyDocument(const Document& document);
	static bool writeMirrorFile(const QString& path, const QByteArray& data, bool fresh);
	static bool linkMirrorEntry(const QString& path, const QString& target, bool fresh);
	static void removeMirrorEntry(const QString& path);
	static inline bool toolingEnabled = false;
	static inline QFile* toolingLock = nullptr;

#ifdef QS_TEST
	friend class ::TestToolSupport;
#endif
};

} // namespace qs::core
