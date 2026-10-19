#include "markdownrenderer.h"
#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextCursor>
#include <QTextFormat>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QUrl>

namespace {
class IsolatedDocument : public QTextDocument {
    QVariant loadResource(int,const QUrl &) override { return {}; }
};
bool allowedLink(const QString &link) { const QUrl url(link); return url.isValid()&&!url.host().isEmpty()&&(url.scheme()=="https"||url.scheme()=="http"); }
}
QString MarkdownRenderer::render(const QString &markdown) const {
    IsolatedDocument document;
    document.setDefaultFont(QGuiApplication::font());
    document.setMarkdown(markdown,QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)|QTextDocument::MarkdownNoHTML);
    // Image resources must never reach the QML renderer, including file/data URLs.
    QList<QPair<int,int>> images;
    for(auto block=document.begin();block.isValid();block=block.next()) {
        for(auto it=block.begin();!it.atEnd();++it) {
            const auto fragment=it.fragment(); const auto format=fragment.charFormat();
            if(format.isImageFormat()) images.append({fragment.position(),fragment.length()});
            else if(format.isAnchor()&&!allowedLink(format.anchorHref())) {
                QTextCursor cursor(&document);cursor.setPosition(fragment.position());cursor.setPosition(fragment.position()+fragment.length(),QTextCursor::KeepAnchor);
                auto clean=format;clean.setAnchor(false);clean.setAnchorHref({});cursor.setCharFormat(clean);
            }
        }
    }
    for(auto it=images.crbegin();it!=images.crend();++it) {
        QTextCursor cursor(&document);cursor.setPosition(it->first);cursor.setPosition(it->first+it->second,QTextCursor::KeepAnchor);cursor.insertText(QStringLiteral("[Image omitted]"));
    }
    const auto palette=QGuiApplication::palette();
    const bool dark=palette.color(QPalette::Base).lightness()<128;
    const QRegularExpression keywords(QStringLiteral("\\b(class|struct|public|private|const|auto|void|int|float|double|return|if|else|for|while|def|import|from|as|let|var|function|async|await|fn|pub|use|true|false|null|None|True|False|SELECT|FROM|WHERE)\\b"));
    const QRegularExpression literals(QStringLiteral(R"("([^"\\]|\\.)*"|'([^'\\]|\\.)*'|//[^\n]*|#[^\n]*)"));
    for(auto block=document.begin();block.isValid();block=block.next()) {
        if(!block.blockFormat().hasProperty(QTextFormat::BlockCodeFence)&&!block.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage)) continue;
        QTextCursor cursor(block);cursor.movePosition(QTextCursor::EndOfBlock,QTextCursor::KeepAnchor);
        QTextCharFormat code;code.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));code.setForeground(palette.color(QPalette::Text));cursor.mergeCharFormat(code);
        auto apply=[&](const QRegularExpression &expression,const QColor &color,bool bold){
            auto matches=expression.globalMatch(block.text());
            while(matches.hasNext()) {const auto match=matches.next(); QTextCursor token(&document);token.setPosition(block.position()+match.capturedStart());token.setPosition(block.position()+match.capturedEnd(),QTextCursor::KeepAnchor);QTextCharFormat format;format.setForeground(color);if(bold)format.setFontWeight(QFont::Bold);token.mergeCharFormat(format);}
        };
        apply(keywords,palette.color(QPalette::Link),true);
        apply(literals,dark?QColor("#8bd49c"):QColor("#236d3a"),false);
    }
    return document.toHtml();
}
bool MarkdownRenderer::openLink(const QString &link) const {return allowedLink(link)&&QDesktopServices::openUrl(QUrl(link));}
