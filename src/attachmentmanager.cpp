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
#include <QDesktopServices>
#include <QImage>
#include <QBuffer>
#include <QSet>

static constexpr qint64 MaximumFileBytes=20*1024*1024;
AttachmentManager::AttachmentManager(const QString &root,QObject *parent):QObject(parent),m_root(root) { m_pool.setMaxThreadCount(2); }
AttachmentManager::~AttachmentManager() { for(auto *job:std::as_const(m_jobs))job->cancel(); for(auto *job:std::as_const(m_jobs))job->waitForFinished(); }
QVariantList AttachmentManager::entries() const {auto result=allEntries();for(qsizetype i=result.size();i-->0;)if(result.at(i).toMap().value("sent").toBool())result.removeAt(i);return result;}
QVariantList AttachmentManager::allEntries() const { QVariantList result; for(auto entry:m_entries) {entry.remove("text");entry.remove("image");entry.remove("sources");result.append(entry);} return result; }
QString AttachmentManager::directory(const QString &id) const { return m_root+"/"+m_conversation+"/"+id; }
QJsonArray AttachmentManager::metadata() const {
    QJsonArray result;
    for(auto entry:m_entries) { entry.remove("text");entry.remove("image");entry.remove("sources");result.append(QJsonObject::fromVariantMap(entry)); }
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
        entry["sent"]=entry.value("sent",false);
        const auto state=entry.value("state").toString();
        entry["state"]=state=="ready"?"queued":"cancelled";
        entry["error"]=state=="ready"?QStringLiteral(""):i18n("Preparation was interrupted. Retry or remove this file.");
        m_entries.append(entry);
    }
    emit changed();
    const auto snapshot=m_entries;
    for(const auto &entry:snapshot) if(entry.value("state")=="queued") start(entry.value("id").toString(),directory(entry.value("id").toString())+"/original");
}
QString AttachmentManager::issue() const {return issueFor(QJsonArray::fromVariantList(allEntries()));}
QString AttachmentManager::issueFor(const QJsonArray &files) const {
    if(busy())return i18n("Files are being prepared. Wait, or cancel and remove them before sending.");
    const auto entries=selected(files);
    QSet<QString> wanted;for(const auto &file:files)wanted.insert(file.toObject().value("id").toString());
    if(entries.size()!=wanted.size())return i18n("A message attachment is unavailable. Remove it from its message or restore the file.");
    int images=0;qint64 imageBytes=0;
    for(const auto &entry:entries) {
        if(entry.value("state")!="ready")return i18n("Retry or remove files that are not ready, including attachments on earlier messages.");
        if(entry.value("kind")=="image") {
            ++images;imageBytes+=entry.value("size").toLongLong();
            if(!m_visionAllowed)return i18n("Select a model with confirmed vision support, or remove the images from their messages before sending.");
        }
    }
    if(images>4 || imageBytes>20*1024*1024)return i18n("Use at most 4 images with a combined size of 20 MiB.");
    return {};
}
QList<QVariantMap> AttachmentManager::selected(const QJsonArray &files) const {
    QList<QVariantMap> result;QSet<QString> seen;
    for(const auto &file:files){const auto id=file.toObject().value("id").toString();if(seen.contains(id))continue;seen.insert(id);for(const auto &entry:m_entries)if(entry.value("id")==id){result.append(entry);break;}}
    return result;
}
QJsonArray AttachmentManager::pendingFiles() const {
    QJsonArray result;for(const auto &entry:m_entries)if(!entry.value("sent").toBool())result.append(QJsonObject{{"id",entry.value("id").toString()},{"name",entry.value("name").toString()},{"kind",entry.value("kind").toString()}});return result;
}
void AttachmentManager::setSent(const QJsonArray &files,bool sent) {
    QSet<QString> ids;for(const auto &file:files)ids.insert(file.toObject().value("id").toString());
    for(auto &entry:m_entries)if(ids.contains(entry.value("id").toString()))entry["sent"]=sent;
    emit changed();
}
void AttachmentManager::discardUnusedSent(const QJsonArray &used) {
    QSet<QString> ids;for(const auto &file:used)ids.insert(file.toObject().value("id").toString());
    QStringList discarded;for(const auto &entry:m_entries)if(entry.value("sent").toBool()&&!ids.contains(entry.value("id").toString()))discarded.append(entry.value("id").toString());
    for(const auto &id:discarded)remove(id);
}
void AttachmentManager::addFiles(const QList<QUrl> &urls) {
    if(QUuid(m_conversation).isNull()) { emit notice(i18n("Create a conversation before attaching files.")); return; }
    for(const auto &url:urls) {
        if(m_entries.size()>=8) { emit notice(i18n("Attach at most 8 files per conversation.")); break; }
        if(!url.isLocalFile()) { emit notice(i18n("Only local file URLs are accepted. Paste ordinary text into the message box.")); continue; }
        const QFileInfo info(url.toLocalFile());
        if(!info.isFile()) { emit notice(i18n("Choose a regular file, not a folder or device.")); continue; }
        const bool image=QMimeDatabase().mimeTypeForFile(info, QMimeDatabase::MatchContent).name().startsWith("image/");
        if(image && !m_visionAllowed) {emit notice(i18n("Image input requires a model with confirmed vision support. Refresh model details or select a vision model."));continue;}
        int imageCount=0;for(const auto &entry:m_entries)if(entry.value("kind")=="image")++imageCount;
        if(image && imageCount>=4){emit notice(i18n("Attach at most 4 images."));continue;}
        const auto path=info.canonicalFilePath();
        bool duplicate=false;
        for(const auto &entry:m_entries) if(entry.value("source")==path)duplicate=true;
        if(duplicate) {emit notice(i18n("This file is already attached to this conversation."));continue;}
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_entries.append({{"id",id},{"name",info.fileName()},{"source",path},{"size",info.size()},{"kind",image?"image":"text"},{"sent",false},{"state","queued"},{"error",""},{"progress",0}});
        emit changed(); start(id,path);
    }
}
void AttachmentManager::start(const QString &id,const QString &source,const QImage &clipboardImage) {
    if(m_jobs.contains(id))return;
    const auto folder=directory(id);
    auto *watcher=new QFutureWatcher<IngestResult>(this); m_jobs.insert(id,watcher);
    for(auto &entry:m_entries) if(entry.value("id")==id) {entry["state"]="extracting";entry["error"]="";entry["progress"]=0;}
    emit changed();
    connect(watcher,&QFutureWatcher<IngestResult>::progressTextChanged,this,[this,id](const QString &stage){for(auto &e:m_entries)if(e.value("id")==id)e["stage"]=stage;emit changed();});
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
            entry["kind"]=result.kind;entry["image"]=result.image;entry["sources"]=result.sources;entry["warning"]=result.warning;entry["textBytes"]=result.text.toUtf8().size();entry["sourceCount"]=result.sources.size();
            entry["state"]=result.error.isEmpty()?"ready":watcher->isCanceled()?"cancelled":"error";
            entry["error"]=result.error; entry["text"]=result.text;entry["hash"]=result.hash; entry["size"]=result.size;entry["progress"]=100;
        }
        if(!retained) QDir(folder).removeRecursively();
        watcher->deleteLater(); emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[source,folder,clipboardImage](QPromise<IngestResult> &promise){
        IngestResult result;
        auto fail=[&](const QString &message){result.error=message;promise.addResult(result);};
        promise.setProgressRange(0,100);
        QByteArray bytes;
        if(!clipboardImage.isNull()) {
            promise.setProgressValueAndText(0,i18n("Encoding clipboard image…"));
            if(promise.isCanceled())return;
            QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
            if(!clipboardImage.save(&buffer,"PNG") || bytes.size()>10*1024*1024){fail(i18n("Clipboard image exceeds 10 MiB or cannot be encoded."));return;}
        } else {
            QFile input(source);
            if(!QFileInfo(source).isFile()||!input.open(QIODevice::ReadOnly)) {fail(i18n("Cannot read this file. Check its location and permissions."));return;}
            if(input.size()>MaximumFileBytes) {fail(i18n("File exceeds the 20 MiB input limit. Use a smaller document."));return;}
            promise.setProgressValueAndText(0,i18n("Reading file…"));
            while(!input.atEnd()) {
                if(promise.isCanceled())return;
                const auto chunk=input.read(64*1024);
                if(chunk.isEmpty()&&input.error()!=QFileDevice::NoError) {fail(i18n("Reading the file failed."));return;}
                bytes+=chunk;
                if(bytes.size()>MaximumFileBytes) {fail(i18n("File exceeds the 20 MiB input limit."));return;}
                promise.setProgressValue(input.size()>0?int(bytes.size()*25/input.size()):25);
            }
        }
        if(bytes.isEmpty()) {fail(i18n("This file is empty."));return;}
        if(promise.isCanceled())return;
        if(!QDir().mkpath(folder)) {fail(i18n("Cannot create the managed attachment directory."));return;}
        const auto original=folder+"/original";
        if(!clipboardImage.isNull() || QFileInfo(source).absoluteFilePath()!=QFileInfo(original).absoluteFilePath()) {
            QSaveFile copy(original);
            if(!copy.open(QIODevice::WriteOnly)||copy.write(bytes)!=bytes.size()||!copy.commit()) {fail(i18n("Saving the attachment copy failed."));return;}
            QFile::setPermissions(original,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        }
        promise.setProgressValue(30);
        result=extractFile(bytes,original,promise);
        result.size=bytes.size();result.hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        if(!result.error.isEmpty()){promise.addResult(result);return;}
        if(promise.isCanceled())return;
        promise.setProgressValueAndText(95,i18n("Saving prepared content…"));
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
QString AttachmentManager::referenceText(QString *error) const {if(error)*error=issue();if(!issue().isEmpty())return {};return referenceTextFor(QJsonArray::fromVariantList(allEntries()));}
QString AttachmentManager::referenceTextFor(const QJsonArray &files) const {
    QString output;
    for(const auto &entry:selected(files)) {
        if(entry.value("kind")=="image")continue;
        const auto name=QString::fromUtf8(QJsonDocument(QJsonArray{entry.value("name").toString()}).toJson(QJsonDocument::Compact));
        const auto tag=entry.value("id").toString();
        output+="\n[BEGIN FILE "+tag+" "+name+"]\n"+entry.value("text").toString()+"\n[END FILE "+tag+"]\n";
    }
    return output;
}
void AttachmentManager::deleteCopies(const QString &conversation) { if(!busy()&&!QUuid(conversation).isNull()) QDir(m_root+"/"+conversation).removeRecursively(); }
bool AttachmentManager::pasteFiles(bool allowed) {
    const auto *mime=QGuiApplication::clipboard()->mimeData();
    if(!mime)return false;
    if(mime->hasUrls()){if(allowed)addFiles(mime->urls());else emit notice(i18n("Stop generation before attaching files."));return true;}
    if(!mime->hasImage())return false;
    if(!allowed){emit notice(i18n("Stop generation before attaching images."));return true;}
    if(!m_visionAllowed){emit notice(i18n("Select a model with confirmed vision support before pasting images."));return true;}
    int count=0;for(const auto &entry:m_entries)if(entry.value("kind")=="image")++count;
    if(count>=4 || m_entries.size()>=8 || QUuid(m_conversation).isNull()){emit notice(i18n("Use at most 8 attachments, including at most 4 images."));return true;}
    const auto image=qvariant_cast<QImage>(mime->imageData());
    if(image.isNull() || image.width()>8192 || image.height()>8192 || qint64(image.width())*image.height()>12000000){emit notice(i18n("Clipboard image is invalid or exceeds the image dimensions limit."));return true;}
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);const auto folder=directory(id);
    m_entries.append({{"id",id},{"name",i18n("Clipboard image.png")},{"source",folder+"/original"},{"kind","image"},{"sent",false},{"state","queued"},{"size",0},{"error",""},{"progress",0}});
    emit changed();start(id,folder+"/original",image);return true;
}
void AttachmentManager::setVisionAllowed(bool allowed) {if(m_visionAllowed==allowed)return;m_visionAllowed=allowed;emit changed();}
bool AttachmentManager::hasImages() const {for(const auto &entry:m_entries)if(entry.value("kind")=="image")return true;return false;}
QJsonArray AttachmentManager::images() const {return imagesFor(QJsonArray::fromVariantList(allEntries()));}
QJsonArray AttachmentManager::imagesFor(const QJsonArray &files) const {QJsonArray result;for(const auto &entry:selected(files))if(entry.value("kind")=="image" && entry.value("state")=="ready")result.append(entry.value("image").toString());return result;}
QUrl AttachmentManager::imageUrl(const QString &id) const {for(const auto &entry:m_entries)if(entry.value("id")==id && entry.value("kind")=="image" && entry.value("state")=="ready")return QUrl::fromLocalFile(directory(id)+"/original");return {};}
QVariantList AttachmentManager::sources(const QString &id) const {QVariantList result;for(const auto &entry:m_entries)if(entry.value("id")==id)for(const auto &source:entry.value("sources").toList()){auto row=source.toMap();row.remove("text");result.append(row);}return result;}
QString AttachmentManager::sourceText(const QString &id,int index) const {for(const auto &entry:m_entries)if(entry.value("id")==id){const auto rows=entry.value("sources").toList();if(index>=0 && index<rows.size())return rows.at(index).toMap().value("text").toString();}return {};}
void AttachmentManager::openSource(const QString &id,int index) {
    for(const auto &entry:m_entries)if(entry.value("id")==id && entry.value("state")=="ready"){
        const auto rows=entry.value("sources").toList();if(index<0 || index>=rows.size())return;
        const auto kind=entry.value("kind").toString();if(kind!="pdf" && kind!="docx")return;
        // Give the desktop a proper extension while retaining the managed original.
        const auto original=QFileInfo(directory(id)+"/original").absoluteFilePath();
        const auto view=directory(id)+"/source."+kind;
        if(!QFile::exists(view) && !QFile::link(original,view)){emit notice(i18n("Cannot open the managed source document."));return;}
        auto url=QUrl::fromLocalFile(view);
        const auto page=rows.at(index).toMap().value("page").toInt();if(page>0)url.setFragment("page="+QString::number(page));
        QDesktopServices::openUrl(url);return;
    }
}

qint64 AttachmentManager::referenceByteEstimate() const {return referenceByteEstimateFor(QJsonArray::fromVariantList(allEntries()));}
qint64 AttachmentManager::referenceByteEstimateFor(const QJsonArray &files) const {
    qint64 total=files.isEmpty()?0:256;
    for(const auto &entry:selected(files))total+=(entry.value("kind")=="image"?4096:entry.value("textBytes",entry.value("size")).toLongLong())+128+QJsonDocument(QJsonArray{entry.value("name").toString()}).toJson(QJsonDocument::Compact).size();
    return total;
}
