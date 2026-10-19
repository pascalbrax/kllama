#include "fileextractor.h"
#include <KLocalizedString>
#include <QBuffer>
#include <QElapsedTimer>
#include <QImageReader>
#include <QImage>
#include <QMimeDatabase>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtEndian>

namespace {
constexpr qint64 TextLimit = 512 * 1024;
struct CommandResult { QByteArray output; QString error; };
CommandResult command(const QString &program, const QStringList &arguments, qint64 limit, QPromise<IngestResult> &promise) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    process.setProcessEnvironment(environment);
    process.start(program, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(3000)) return {{}, i18n("Cannot start %1. Install poppler-utils and unzip, then retry.", program)};
    process.closeWriteChannel();
    QElapsedTimer timer; timer.start();
    CommandResult result;
    QByteArray errors;
    for (;;) {
        process.waitForReadyRead(20);
        result.output += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if (promise.isCanceled() || timer.elapsed() > 45000 || result.output.size() > limit || errors.size() > 16384) {
            process.kill(); process.waitForFinished(3000);
            return {{}, promise.isCanceled() ? i18n("Preparation cancelled.") : i18n("Document extraction exceeded its time or output limit. Use a smaller document.")};
        }
        if (process.state() == QProcess::NotRunning) break;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = errors.toLower().contains("password")
            ? i18n("Encrypted or password-protected documents are not supported. Save an unencrypted copy and attach it.")
            : i18n("%1 could not extract this document. It may be encrypted, damaged, or unsupported.", program);
    }
    return result;
}
}

IngestResult extractFile(const QByteArray &bytes, const QString &path, QPromise<IngestResult> &promise) {
    IngestResult result;
    const auto mime = QMimeDatabase().mimeTypeForData(bytes).name();
    auto fail = [&](const QString &message) { result.error = message; return result; };
    qint64 extractedBytes = 0;
    auto addSource = [&](const QString &label, const QString &text, int page = 0) {
        result.sources.append(QVariantMap{{"label",label},{"text",text},{"page",page}});
        const auto excerpt = "\n[" + label + "]\n" + text + "\n";
        extractedBytes += excerpt.toUtf8().size();
        result.text += excerpt;
    };
    if (mime.startsWith("image/")) {
        result.kind = "image";
        promise.setProgressValueAndText(35,i18n("Validating image…"));
        if (bytes.size() > 10 * 1024 * 1024) return fail(i18n("Image exceeds the 10 MiB limit."));
        QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setDecideFormatFromContent(true);
        const auto format = reader.format();
        if (format != "png" && format != "jpeg" && format != "jpg") return fail(i18n("Use a PNG or JPEG image. Other image formats are not supported yet."));
        if (format == "png") {
            qsizetype offset = 8;
            while (offset + 12 <= bytes.size()) {
                const auto length = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + offset));
                if (length > quint64(bytes.size() - offset - 12)) break;
                if (bytes.mid(offset + 4, 4) == "acTL") return fail(i18n("Animated PNGs are not supported. Attach a single image frame."));
                offset += qsizetype(length) + 12;
            }
        }
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 8192 || size.height() > 8192 || qint64(size.width()) * size.height() > 12000000)
            return fail(i18n("Image exceeds 8,192 pixels on one side or 12 megapixels. Resize it before attaching."));
        if (reader.imageCount() > 1) return fail(i18n("Animated images are not supported. Attach a single PNG or JPEG frame."));
        if (reader.read().isNull()) return fail(i18n("Cannot decode this image. It may be damaged or use an unsupported encoding."));
        result.width = size.width(); result.height = size.height();
        result.image = QString::fromLatin1(bytes.toBase64());
        result.text = i18n("Image: %1 × %2 pixels. Sent as native image input; no OCR text was extracted.", result.width, result.height);
        return result;
    }
    if (bytes.startsWith("%PDF-")) {
        result.kind = "pdf";
        promise.setProgressValueAndText(35,i18n("Inspecting PDF…"));
        auto info = command("pdfinfo", {path}, 64 * 1024, promise);
        if (!info.error.isEmpty()) return fail(info.error);
        const auto details = QString::fromUtf8(info.output);
        if (QRegularExpression("(?m)^Encrypted:\\s+yes").match(details).hasMatch())
            return fail(i18n("Encrypted PDFs are not supported. Save an unencrypted copy and attach it."));
        const auto match = QRegularExpression("(?m)^Pages:\\s+(\\d+)").match(details);
        const int pages = match.captured(1).toInt();
        if (pages <= 0 || pages > 200) return fail(i18n("PDF must contain between 1 and 200 pages."));
        promise.setProgressValueAndText(40,i18n("Extracting PDF text…"));
        auto extracted = command("pdftotext", {"-layout", "-enc", "UTF-8", path, "-"}, TextLimit, promise);
        if (!extracted.error.isEmpty()) return fail(extracted.error);
        promise.setProgressValueAndText(50,i18n("Preparing page references…"));
        auto parts = QString::fromUtf8(extracted.output).split(QChar('\f'));
        int empty = 0;
        for (int page = 0; page < pages; ++page) {
            if (promise.isCanceled()) return fail(i18n("Preparation cancelled."));
            auto text = parts.value(page).trimmed();
            if (text.isEmpty()) { ++empty; text = i18n("No extractable text on this page; OCR may be required."); }
            addSource(i18n("Page %1", page + 1), text, page + 1);
            promise.setProgressValue(50 + (page + 1) * 40 / pages);
        }
        if (empty == pages) return fail(i18n("This PDF has no extractable text. Scanned PDFs need OCR, which is not included yet."));
        if (empty) result.warning = i18np("%1 page has no extractable text and may need OCR.", "%1 pages have no extractable text and may need OCR.", empty);
    } else if (bytes.startsWith("PK\003\004")) {
        result.kind = "docx";
        promise.setProgressValueAndText(35,i18n("Inspecting DOCX…"));
        const auto listing = command("unzip", {"-Z", "-1", path}, 256 * 1024, promise);
        if (!listing.error.isEmpty()) return fail(listing.error);
        const auto files = listing.output.split('\n');
        if (files.size() > 4096 || files.count("word/document.xml") != 1 || files.count("[Content_Types].xml") != 1)
            return fail(i18n("This ZIP archive is not a supported DOCX document."));
        auto extracted = command("unzip", {"-p", "-P", "", path, "word/document.xml"}, 8 * 1024 * 1024, promise);
        if (!extracted.error.isEmpty()) return fail(extracted.error);
        promise.setProgressValueAndText(45,i18n("Reading DOCX paragraphs…"));
        QXmlStreamReader xml(extracted.output);
        const QString wordNamespace = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
        const QString strictNamespace = QStringLiteral("http://purl.oclc.org/ooxml/wordprocessingml/main");
        bool inParagraph = false, heading = false, document = false;
        QString paragraph, section;
        int number = 0;
        while (!xml.atEnd()) {
            if (promise.isCanceled()) return fail(i18n("Preparation cancelled."));
            xml.readNext();
            if (xml.isDTD() || xml.isEntityReference()) return fail(i18n("DOCX XML with entity declarations is not supported."));
            if (xml.namespaceUri() != wordNamespace && xml.namespaceUri() != strictNamespace) continue;
            if (xml.isStartElement()) {
                if (xml.name() == u"txbxContent" || xml.name() == u"del") { xml.skipCurrentElement(); continue; }
                if (xml.name() == u"document") document = true;
                else if (xml.name() == u"p") { inParagraph = true; heading = false; paragraph.clear(); ++number; }
                else if (inParagraph && xml.name() == u"pStyle") {
                    const auto style = xml.attributes().value(xml.namespaceUri(), u"val").toString();
                    heading = style.startsWith("Heading", Qt::CaseInsensitive) || style == "Title";
                } else if (inParagraph && xml.name() == u"t") paragraph += xml.readElementText();
                else if (inParagraph && xml.name() == u"tab") paragraph += '\t';
                else if (inParagraph && (xml.name() == u"br" || xml.name() == u"cr")) paragraph += '\n';
            } else if (xml.isEndElement() && xml.name() == u"p" && inParagraph) {
                inParagraph = false;
                if (!paragraph.trimmed().isEmpty()) {
                    if (heading) section = paragraph.trimmed().left(160);
                    addSource(section.isEmpty() ? i18n("Paragraph %1", number) : i18n("%1 · Paragraph %2", section, number), paragraph);
                }
            }
            if (extractedBytes > TextLimit || number > 20000) return fail(i18n("DOCX extracted text exceeds 512 KiB or 20,000 paragraphs. Use a smaller document."));
        }
        if (xml.hasError() || !document) return fail(i18n("The DOCX document XML is damaged or unsupported."));
        if (result.sources.isEmpty()) return fail(i18n("This DOCX has no extractable body text. Embedded images need OCR; headers, footers, comments and text boxes are not included."));
        result.warning = i18n("Extracted document body paragraphs, including table text. Headers, footers, footnotes, endnotes, comments, text boxes and embedded images are not included.");
    } else {
        if (bytes.size() > TextLimit) return fail(i18n("Text file exceeds the 512 KiB limit. Shorten it or use a smaller file."));
        if (mime.startsWith("audio/") || mime.startsWith("video/") || mime.contains("zip") || mime.contains("compressed") || mime == "application/pdf")
            return fail(i18n("Unsupported file content. Attach UTF-8 text, PDF, DOCX, PNG or JPEG."));
        QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        result.text = decoder(bytes);
        if (decoder.hasError()) return fail(i18n("This file is not valid UTF-8. Convert its encoding before attaching."));
        for (const auto ch : result.text)
            if (ch.unicode() < 32 && ch != '\n' && ch != '\r' && ch != '\t' && ch != '\f')
                return fail(i18n("Binary file content is not supported."));
    }
    if (result.text.toUtf8().size() > TextLimit) return fail(i18n("Extracted text exceeds 512 KiB. Use a smaller document."));
    return result;
}
