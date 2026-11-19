#include "toolsupport.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <memory>

#include <dlfcn.h>
#include <fcntl.h>
#include <qbytearray.h>
#include <qcontainerfwd.h>
#include <qcryptographichash.h>
#include <qdatetime.h>
#include <qdeadlinetimer.h>
#include <qdebug.h>
#include <qdir.h>
#include <qendian.h>
#include <qfile.h>
#include <qfiledevice.h>
#include <qfileinfo.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qqmlengine.h>
#include <qsavefile.h>
#include <qset.h>
#include <qtenvironmentvariables.h>
#include <qthread.h>
#include <qthreadpool.h>
#include <qtypes.h>

#include "build.hpp"
#include "logcat.hpp"
#include "paths.hpp"
#include "scan.hpp"

namespace qs::core {

namespace {
QS_LOGGING_CATEGORY(logTooling, "quickshell.tooling", QtWarningMsg);

bool scannerVisited(const QmlScanner& scanner, const QString& prefix) {
	auto under = [&](const QString& path) { return path.startsWith(prefix); };
	return std::ranges::any_of(scanner.scannedFiles, under)
	    || std::any_of(scanner.fileIntercepts.keyBegin(), scanner.fileIntercepts.keyEnd(), under);
}
} // namespace

bool QmlToolingSupport::updateTooling(const QDir& configRoot, bool keepMirror) {
	auto* vfs = QsPaths::instance()->shellVfsDir();

	if (!vfs) {
		qCCritical(logTooling) << "Tooling dir could not be created";
		return false;
	}

	if (QmlToolingSupport::lockTooling() != LockResult::Acquired) {
		return false;
	}

	if (QmlToolingSupport::updateQmllsConfig(configRoot, false)) return true;
	if (!keepMirror) QmlToolingSupport::removeMirrorEntry(vfs->filePath("qs"));
	return false;
}

QString QmlToolingSupport::updateMirror(
    const QDir& configRoot,
    const QmlScanner& scanner,
    bool copyDocuments
) {
	auto* vfs = QsPaths::instance()->shellVfsDir();
	if (!vfs) return QString();
	if (QmlToolingSupport::lockTooling() != LockResult::Acquired) return QString();

	// QQmlTypeLoader::addImportPath canonicalizes, so urls built from this must match
	auto vfsPath = vfs->canonicalPath();
	if (vfsPath.isEmpty()) return QString();

	auto target = QDir(vfsPath % "/qs");
	QList<Document> documents;

	if (!QmlToolingSupport::mirrorDir(
	        scanner,
	        configRoot,
	        target,
	        copyDocuments ? &documents : nullptr
	    )
	    || !QmlToolingSupport::copyDocuments(documents))
	{
		return QString();
	}

	return vfsPath;
}

bool QmlToolingSupport::writeTooling(const QDir& configRoot, const QmlScanner& scanner) {
	auto* vfs = QsPaths::instance()->shellVfsDir();
	if (!vfs) {
		qCCritical(logTooling) << "Tooling dir could not be created";
		return false;
	}

	auto lock = QmlToolingSupport::lockTooling(true);
	if (lock == LockResult::Failed) return false;

	// A running instance of this config holds the lock; its files only count once it has linked
	// the config and built the mirror.
	if (lock == LockResult::Contended) {
		auto vfsConfig = QFileInfo(vfs->filePath(".qmlls.ini")).canonicalFilePath();
		return !vfsConfig.isEmpty()
		    && QFileInfo(configRoot.filePath(".qmlls.ini")).canonicalFilePath() == vfsConfig
		    && QFileInfo::exists(vfs->filePath("qs/qmldir"));
	}

	auto written = QmlToolingSupport::updateQmllsConfig(configRoot, true)
	            && !QmlToolingSupport::updateMirror(configRoot, scanner, false).isEmpty();

	// Release before process teardown, as a starting instance waits on it.
	delete QmlToolingSupport::toolingLock;
	QmlToolingSupport::toolingLock = nullptr;

	return written;
}

// A boot copies every document, which is most of the mirror's cost on one thread.
bool QmlToolingSupport::copyDocuments(const QList<Document>& documents) {
	auto pool = QThreadPool();
	const auto workers = pool.maxThreadCount();
	std::atomic<bool> copied = true;

	for (auto worker = 0; worker < workers; worker++) {
		pool.start([&documents, &copied, worker, workers] {
			for (auto i = worker; i < documents.size(); i += workers) {
				if (!QmlToolingSupport::copyDocument(documents.at(i))) copied = false;
			}
		});
	}

	pool.waitForDone();
	return copied;
}

bool QmlToolingSupport::copyDocument(const Document& document) {
	auto file = QFile(document.source);

	if (!file.open(QFile::ReadOnly)) {
		qCCritical(logTooling) << "Could not read" << document.source;
		return false;
	}

	return QmlToolingSupport::writeMirrorFile(document.target, file.readAll(), document.fresh);
}

bool QmlToolingSupport::mirrorDir(
    const QmlScanner& scanner,
    const QDir& source,
    const QDir& target,
    QList<Document>* documents
) {
	auto targetInfo = QFileInfo(target.path());
	if (targetInfo.isSymLink() || (targetInfo.exists() && !targetInfo.isDir())) {
		QmlToolingSupport::removeMirrorEntry(target.path());
		targetInfo.refresh();
	}

	// A dir created here has nothing to compare against or clean up, which is every dir on boot.
	const auto fresh = !targetInfo.exists();

	if (!target.mkpath(".")) {
		qCCritical(logTooling) << "Could not create mirror dir at" << target.path();
		return false;
	}

	const QString prefix = source.path() % '/';
	QSet<QString> names;

	for (auto [path, text]: scanner.fileIntercepts.asKeyValueRange()) {
		if (!path.startsWith(prefix)) continue;

		auto name = path.sliced(prefix.length());
		if (name.contains('/')) continue;

		names.insert(name);

		if (!QmlToolingSupport::writeMirrorFile(target.filePath(name), text.toUtf8(), fresh)) {
			return false;
		}
	}

	for (const auto& name: source.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
		if (names.contains(name)) continue;
		names.insert(name);

		auto path = source.filePath(name);
		auto entryTarget = target.filePath(name);

		if (!QmlToolingSupport::mirrorEntry(scanner, path, entryTarget, documents, fresh)) {
			return false;
		}
	}

	if (fresh) return true;

	auto stale = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;

	for (const auto& name: target.entryList(stale)) {
		if (!names.contains(name)) QmlToolingSupport::removeMirrorEntry(target.filePath(name));
	}

	return true;
}

bool QmlToolingSupport::mirrorEntry(
    const QmlScanner& scanner,
    const QString& path,
    const QString& target,
    QList<Document>* documents,
    bool fresh
) {
	auto info = QFileInfo(path);

	if (info.isDir()) {
		// symlinked dirs can loop, so only the ones the scanner went through are entered
		if (info.isSymLink() && !scannerVisited(scanner, path % '/')) {
			return QmlToolingSupport::linkMirrorEntry(path, target, fresh);
		}

		return QmlToolingSupport::mirrorDir(scanner, QDir(path), QDir(target), documents);
	}

	auto name = info.fileName();
	auto isDocument = name.endsWith(".qml") || name.endsWith(".js") || name.endsWith(".mjs");
	// A copy costs runtime dir memory; only sources without a usable mtime (the nix store's 1)
	// need one for the disk cache.
	if (!documents || !isDocument || info.lastModified().toSecsSinceEpoch() > 1) {
		return QmlToolingSupport::linkMirrorEntry(path, target, fresh);
	}

	documents->append({.source = path, .target = target, .fresh = fresh});
	return true;
}

bool QmlToolingSupport::writeMirrorFile(const QString& path, const QByteArray& data, bool fresh) {
	// The disk cache compares source mtimes (QV4::CompiledData::Unit::verifyHeader), which
	// are all 1 in the nix store, so the mtime comes from the content. 2000..2030, never 0.
	const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
	const auto secs = 946684800 + qFromBigEndian<quint32>(hash.constData()) % 946684800;
	const auto time = QDateTime::fromSecsSinceEpoch(secs);
	auto file = QFile(path);

	if (fresh) {
		// Unbuffered, or the write lands after the time is set.
		if (!file.open(QFile::WriteOnly | QFile::Unbuffered) || file.write(data) != data.length()
		    || !file.setFileTime(time, QFile::FileModificationTime))
		{
			qCCritical(logTooling) << "Failed to write mirror file" << path;
			return false;
		}

		return true;
	}

	auto info = QFileInfo(path);
	if (info.isSymLink() || (info.exists() && !info.isFile())) {
		QmlToolingSupport::removeMirrorEntry(path);
	}

	auto current = QFile(path);
	if (current.open(QFile::ReadOnly) && current.readAll() == data) return true;
	current.close();

	auto save = QSaveFile(path);

	if (!save.open(QFile::WriteOnly) || save.write(data) != data.length() || !save.commit()) {
		qCCritical(logTooling) << "Failed to write mirror file" << path;
		return false;
	}

	if (!file.open(QFile::ReadWrite) || !file.setFileTime(time, QFile::FileModificationTime)) {
		qCCritical(logTooling) << "Failed to set mirror file time on" << path;
		return false;
	}

	qCDebug(logTooling) << "Wrote mirror file" << path;
	return true;
}

bool QmlToolingSupport::linkMirrorEntry(const QString& path, const QString& target, bool fresh) {
	if (!fresh) {
		const auto info = QFileInfo(target);
		if (info.isSymLink() && info.symLinkTarget() == path) return true;

		QmlToolingSupport::removeMirrorEntry(target);
	}

	if (!QFile::link(path, target)) {
		qCCritical(logTooling) << "Could not create symlink to" << path << "at" << target;
		return false;
	}

	// Debug messages always reach the detailed log, one per entry of a new mirror is a cost.
	if (!fresh) qCDebug(logTooling) << "Created symlink to" << path << "at" << target;
	return true;
}

void QmlToolingSupport::removeMirrorEntry(const QString& path) {
	auto info = QFileInfo(path);
	if (!info.exists() && !info.isSymLink()) return;

	// isDir follows symlinks, and everything behind one is the real config
	auto ok =
	    info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path);
	if (!ok) qCWarning(logTooling) << "Failed to remove old file at" << path;
}

QmlToolingSupport::LockResult QmlToolingSupport::lockTooling(bool oneShot) {
	if (QmlToolingSupport::toolingLock) return LockResult::Acquired;

	auto lockPath = QsPaths::instance()->shellVfsDir()->filePath("tooling.lock");
	auto file = std::make_unique<QFile>(lockPath);

	if (!file->open(QFile::WriteOnly)) {
		qCCritical(logTooling) << "Could not open tooling lock for write";
		return LockResult::Failed;
	}

	// One-shot writers lock from byte 1, so a starting instance can tell them apart and wait
	// instead of loading without its mirror. They hold it only while writing.
	struct flock lock = {
	    .l_type = F_WRLCK,
	    .l_whence = SEEK_SET, // NOLINT (fcntl.h??)
	    .l_start = oneShot ? 1 : 0,
	    .l_len = 0,
	    .l_pid = 0,
	};

	// Short for an instance, as it blocks startup.
	auto deadline = QDeadlineTimer(oneShot ? 5000 : 1000);
	while (fcntl(file->handle(), F_SETLK, &lock) != 0) {
		if (errno != EACCES && errno != EAGAIN) {
			qCCritical(logTooling).nospace() << "Could not create tooling lock at " << lockPath
			                                 << " with error code " << errno << ": " << qt_error_string();
			return LockResult::Failed;
		}

		auto holder = lock;
		if (fcntl(file->handle(), F_GETLK, &holder) != 0) return LockResult::Failed;
		if (holder.l_type == F_UNLCK) continue;

		if (holder.l_start != 1 || deadline.hasExpired()) {
			qCInfo(logTooling) << "Tooling support locked by another instance";
			return LockResult::Contended;
		}

		QThread::msleep(10);
	}

	qCInfo(logTooling) << "Acquired tooling support lock";
	QmlToolingSupport::toolingLock = file.release();
	return LockResult::Acquired;
}

// Next to the loaded library first, so a moved prefix still finds them; then the build tree, for
// dev and QTBASE=1 builds; then the configured prefix.
QString QmlToolingSupport::qmlTypesDir() {
	QStringList candidates;

	auto info = Dl_info();
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	if (dladdr(reinterpret_cast<void*>(&QmlToolingSupport::qmlTypesDir), &info) != 0
	    && info.dli_fname)
	{
		candidates.append(QFileInfo(info.dli_fname).dir().filePath(QML_TYPES_LIB_RELATIVE));
	}

	candidates.append(QML_TYPES_BUILD_DIR);
	candidates.append(QML_TYPES_DIR);

	for (const auto& dir: candidates) {
		if (QFileInfo::exists(dir % "/Quickshell/qmldir")) return QDir::cleanPath(dir);
	}

	return QString();
}

QString QmlToolingSupport::getQmllsConfig() {
	static auto config = []() {
		// We can't replicate the algorithm used to create the import path list as it can have distro
		// specific patches, e.g. nixos.
		auto importPaths = QQmlEngine().importPathList();
		importPaths.removeIf([](const QString& path) { return path.startsWith("qrc:"); });

		// First, so linters see this library's Quickshell types over an installed upstream copy.
		auto typesDir = QmlToolingSupport::qmlTypesDir();
		if (!typesDir.isEmpty()) importPaths.prepend(typesDir);

		auto vfsPath = QsPaths::instance()->shellVfsDir()->path();
		auto importPathsStr = importPaths.join(u':');

		QString qmllsConfig;
		auto print = QDebug(&qmllsConfig).nospace();
		print << "[General]\nno-cmake-calls=true\nbuildDir=" << vfsPath
		      << "\nimportPaths=" << importPathsStr << '\n';

		return qmllsConfig;
	}();

	return config;
}

bool QmlToolingSupport::updateQmllsConfig(const QDir& configRoot, bool create) {
	auto shellConfigPath = configRoot.filePath(".qmlls.ini");
	auto vfsConfigPath = QsPaths::instance()->shellVfsDir()->filePath(".qmlls.ini");

	auto shellFileInfo = QFileInfo(shellConfigPath);
	if (!create && !shellFileInfo.exists() && !shellFileInfo.isSymLink()) {
		if (QmlToolingSupport::toolingEnabled) {
			qInfo() << "QML tooling support disabled";
			QmlToolingSupport::toolingEnabled = false;
		} else {
			qCInfo(logTooling) << "Not enabling QML tooling support, qmlls.ini is missing at path"
			                   << shellConfigPath;
		}

		QFile::remove(vfsConfigPath);
		return false;
	}

	auto vfsFile = QFile(vfsConfigPath);

	if (!vfsFile.open(QFile::ReadWrite | QFile::Text)) {
		qCCritical(logTooling) << "Failed to create qmlls config in vfs";
		return false;
	}

	auto config = QmlToolingSupport::getQmllsConfig();

	if (vfsFile.readAll() != config) {
		if (!vfsFile.resize(0) || !vfsFile.write(config.toUtf8())) {
			qCCritical(logTooling) << "Failed to write qmlls config in vfs";
			return false;
		}

		qCDebug(logTooling) << "Wrote qmlls config in vfs";
	}

	if (!shellFileInfo.isSymLink() || shellFileInfo.symLinkTarget() != vfsConfigPath) {
		QFile::remove(shellConfigPath);

		if (!QFile::link(vfsConfigPath, shellConfigPath)) {
			qCCritical(logTooling) << "Failed to create qmlls config symlink";
			return false;
		}

		qCDebug(logTooling) << "Created qmlls config symlink";
	}

	if (!QmlToolingSupport::toolingEnabled) {
		qInfo() << "QML tooling support enabled";
		QmlToolingSupport::toolingEnabled = true;
	}

	return true;
}

} // namespace qs::core
