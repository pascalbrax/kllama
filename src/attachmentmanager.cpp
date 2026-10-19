#include "attachmentmanager.h"
#include <KLocalizedString>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QStringDecoder>
#include <QMimeDatabase>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QtConcurrentRun>
#include <QPromise>

static constexpr qint64 MaximumFileBytes=512*1024;
AttachmentManager::AttachmentManager(const QString &root,QObject *parent):QObject(parent),m_root(root) { m_pool.setMaxThreadCount(2); }
AttachmentManager::~AttachmentManager() { for(auto *job:std::as_const(m_jobs))job->cancel(); for(auto *job:std::as_const(m_jobs))job->waitForFinished(); }
QVariantList AttachmentManager::entries() const { QVariantList result; for(auto entry:m_entries) {entry.remove("text");result.append(entry);} return result; }
QString AttachmentManager::directory(const QString &id) const { return m_root+"/"+m_conversation+"/"+id; }
QJsonArray AttachmentManager::metadata() const {
    QJsonArray result;
    for(auto entry:m_entries) { entry.remove("text");result.append(QJsonObject::fromVariantMap(entry)); }
    return result;
}
void AttachmentManager::restore(const QString &conversation,const QJsonArray &metadata) {
    Q_ASSERT(!busy());
    m_conversation=conversation; m_entries.clear();
    if(QUuid(conversation).isNull()&&!metadata.isEmpty()){emit notice(i18n("Invalid attachment storage identity; no files were loaded."));emit changed();return;}
    for(const auto &value:metadata) {
        auto entry=value.toObject().toVariantMap();
        const auto id=entry.value("id").toString();
        if(QUuid(id).isNull()) continue;
        // Text is loaded by the same bounded background job as new ingestion.
        const auto state=entry.value("state").toString();
        entry["state"]=state=="ready"?"queued":"cancelled";
        entry["error"]=state=="ready"?QStringLiteral(""):i18n("Preparation was interrupted. Retry or remove this file.");
        m_entries.append(entry);
    }
    emit changed();
    const auto snapshot=m_entries;
    for(const auto &entry:snapshot) if(entry.value("state")=="queued") start(entry.value("id").toString(),directory(entry.value("id").toString())+"/original");
}
QString AttachmentManager::issue() const {
    if(busy())return i18n("Files are being prepared. Wait, or cancel and remove them before sending.");
    for(const auto &entry:m_entries) if(entry.value("state")!="ready") return i18n("Remove or retry files that are not ready before sending.");
    return {};
}
void AttachmentManager::addFiles(const QList<QUrl> &urls) {
    if(QUuid(m_conversation).isNull()) { emit notice(i18n("Create a conversation before attaching files.")); return; }
    for(const auto &url:urls) {
        if(m_entries.size()>=8) { emit notice(i18n("Attach at most 8 files per conversation.")); break; }
        if(!url.isLocalFile()) { emit notice(i18n("Only local file URLs are accepted. Paste ordinary text into the message box.")); continue; }
        const QFileInfo info(url.toLocalFile());
        if(!info.isFile()) { emit notice(i18n("Choose a regular text file, not a folder or device.")); continue; }
        const auto path=info.canonicalFilePath();
        bool duplicate=false;
        for(const auto &entry:m_entries) if(entry.value("source")==path)duplicate=true;
        if(duplicate) {emit notice(i18n("This file is already attached."));continue;}
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_entries.append({{"id",id},{"name",info.fileName()},{"source",path},{"size",info.size()},{"state","queued"},{"error",""},{"progress",0}});
        emit changed(); start(id,path);
    }
}
void AttachmentManager::start(const QString &id,const QString &source) {
    if(m_jobs.contains(id))return;
    const auto folder=directory(id);
    auto *watcher=new QFutureWatcher<IngestResult>(this); m_jobs.insert(id,watcher);
    for(auto &entry:m_entries) if(entry.value("id")==id) {entry["state"]="extracting";entry["error"]="";entry["progress"]=0;}
    emit changed();
    connect(watcher,&QFutureWatcher<IngestResult>::progressValueChanged,this,[this,id](int progress){for(auto &e:m_entries)if(e.value("id")==id)e["progress"]=progress;emit changed();});
    connect(watcher,&QFutureWatcher<IngestResult>::finished,this,[this,id,folder,watcher]{
        m_jobs.remove(id);
        IngestResult result;
        if(watcher->isCanceled()||watcher->future().resultCount()==0) result.error=i18n("Preparation cancelled. Retry or remove the file.");
        else result=watcher->result();
        bool retained=false;
        for(auto &entry:m_entries) if(entry.value("id")==id) {
            retained=true;
            if(result.error.isEmpty()) {
                for(const auto &other:m_entries) if(other.value("id")!=id&&other.value("state")=="ready"&&other.value("hash")==result.hash) result.error=i18n("Duplicate content: this file is already included.");
            }
            entry["state"]=result.error.isEmpty()?"ready":watcher->isCanceled()?"cancelled":"error";
            entry["error"]=result.error; entry["text"]=result.text;entry["hash"]=result.hash; entry["size"]=result.size;entry["progress"]=100;
        }
        if(!retained) QDir(folder).removeRecursively();
        watcher->deleteLater(); emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[source,folder](QPromise<IngestResult> &promise){
        IngestResult result;
        auto fail=[&](const QString &message){result.error=message;promise.addResult(result);};
        QFile input(source);
        if(!QFileInfo(source).isFile()||!input.open(QIODevice::ReadOnly)) {fail(i18n("Cannot read this file. Check its location and permissions."));return;}
        if(input.size()>MaximumFileBytes) {fail(i18n("File exceeds the 512 KiB limit. Shorten it or use a smaller file."));return;}
        promise.setProgressRange(0,100);
        QByteArray bytes;
        while(!input.atEnd()) {
            if(promise.isCanceled())return;
            const auto chunk=input.read(64*1024);
            if(chunk.isEmpty()&&input.error()!=QFileDevice::NoError) {fail(i18n("Reading the file failed."));return;}
            bytes+=chunk;
            if(bytes.size()>MaximumFileBytes) {fail(i18n("File exceeds the 512 KiB limit."));return;}
            promise.setProgressValue(input.size()>0?int(bytes.size()*100/input.size()):100);
        }
        if(bytes.isEmpty()) {fail(i18n("This file is empty."));return;}
        const auto mime=QMimeDatabase().mimeTypeForData(bytes).name();
        if(mime.startsWith("image/")||mime.startsWith("audio/")||mime.startsWith("video/")||mime=="application/pdf"||mime.contains("zip")||mime.contains("compressed")) {fail(i18n("M3 accepts text/code files. Native images and document extraction arrive in M4."));return;}
        QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless); result.text=decoder(bytes);
        if(decoder.hasError()) {fail(i18n("This file is not valid UTF-8. Convert its encoding before attaching."));return;}
        for(const auto ch:result.text) if(ch.unicode()<32&&ch!='\n'&&ch!='\r'&&ch!='\t'&&ch!='\f') {fail(i18n("Binary file content is not supported. Attach a UTF-8 text or source file."));return;}
        result.size=bytes.size(); result.hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        if(promise.isCanceled())return;
        if(!QDir().mkpath(folder)) {fail(i18n("Cannot create the managed attachment directory."));return;}
        const auto original=folder+"/original";
        if(QFileInfo(source).absoluteFilePath()!=QFileInfo(original).absoluteFilePath()) {
            QSaveFile copy(original);
            if(!copy.open(QIODevice::WriteOnly)||copy.write(bytes)!=bytes.size()||!copy.commit()) {fail(i18n("Saving the attachment copy failed."));return;}
            QFile::setPermissions(original,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        }
        QSaveFile text(folder+"/text.txt"); const auto extracted=result.text.toUtf8();
        if(!text.open(QIODevice::WriteOnly)||text.write(extracted)!=extracted.size()||!text.commit()) {fail(i18n("Saving extracted text failed."));return;}
        QFile::setPermissions(folder+"/text.txt",QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        if(promise.isCanceled())return;
        promise.setProgressValue(100); promise.addResult(result);
    }));
}
void AttachmentManager::remove(const QString &id) {
    if(auto *job=m_jobs.value(id))job->cancel();
    for(qsizetype i=0;i<m_entries.size();++i)if(m_entries[i].value("id")==id){m_entries.removeAt(i);break;}
    if(!m_jobs.contains(id)&&!QUuid(id).isNull())QDir(directory(id)).removeRecursively();
    emit changed();
}
void AttachmentManager::cancel(const QString &id) { if(auto *job=m_jobs.value(id))job->cancel(); }
void AttachmentManager::retry(const QString &id) {for(const auto &entry:m_entries)if(entry.value("id")==id)start(id,QFile::exists(directory(id)+"/original")?directory(id)+"/original":entry.value("source").toString());}
QString AttachmentManager::preview(const QString &id) const {for(const auto &entry:m_entries)if(entry.value("id")==id)return entry.value("text").toString();return {};}
QString AttachmentManager::referenceText(QString *error) const {
    if(error)*error=issue();
    if(!issue().isEmpty())return {};
    QString output;
    for(const auto &entry:m_entries) {
        const auto name=QString::fromUtf8(QJsonDocument(QJsonArray{entry.value("name").toString()}).toJson(QJsonDocument::Compact));
        const auto tag=entry.value("id").toString();
        output+="\n[BEGIN FILE "+tag+" "+name+"]\n"+entry.value("text").toString()+"\n[END FILE "+tag+"]\n";
    }
    return output;
}
void AttachmentManager::deleteCopies(const QString &conversation) { if(!busy()&&!QUuid(conversation).isNull()) QDir(m_root+"/"+conversation).removeRecursively(); }
bool AttachmentManager::pasteFiles(bool allowed) { const auto *mime=QGuiApplication::clipboard()->mimeData();if(mime&&mime->hasUrls()){if(allowed)addFiles(mime->urls());else emit notice(i18n("Stop generation before attaching files."));return true;}return false; }

qint64 AttachmentManager::referenceByteEstimate() const {
    qint64 total=m_entries.isEmpty()?0:256;
    for(const auto &entry:m_entries)total+=entry.value("size").toLongLong()+128+QJsonDocument(QJsonArray{entry.value("name").toString()}).toJson(QJsonDocument::Compact).size();
    return total;
}
