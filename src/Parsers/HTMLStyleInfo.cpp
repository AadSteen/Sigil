/************************************************************************
**
**  Copyright (C) 2021 Kevin B. Hendricks, Stratford, ON Canada
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include "EmbedPython/EmbeddedPython.h"

#include <QRegularExpression>
#include "Misc/Utility.h"
#include "Parsers/CSSInfo.h"
#include "Parsers/HTMLStyleInfo.h"

static const int TAB_SPACES_WIDTH = 4;
static const QString LINE_MARKER("[SIGIL_NEWLINE]");
static const QString DELIMITERS = "}{;";

// Note: CSSProperties and CSSSelectors are simple struct that this code
// created with new and so need to be manually cleaned up to prevent
// large memory leaks

HTMLStyleInfo::HTMLStyleInfo(const QString &text)
    : m_source(text)
{
    int searchoffset = 0;
    int style_start = -1;
    int style_end = -1;
    while (findInlineStyleBlock(text, searchoffset, style_start, style_end)) {
        CSSInfo* cp = new CSSInfo(text.mid(style_start, style_end - style_start), style_start);
        m_styles.append(cp);
        m_starts.append(style_start);
        m_lengths.append(style_end - style_start);
        searchoffset = style_end;
    }
}


// Need to manually clean up since allocated with new
HTMLStyleInfo::~HTMLStyleInfo()
{
    foreach(CSSInfo * cp, m_styles) {
        if (cp) delete cp;
    }
    m_styles.clear();
    m_starts.clear();
    m_lengths.clear();
}


QList<CSSInfo::CSSSelector *> HTMLStyleInfo::getAllSelectors()
{
    QList<CSSInfo::CSSSelector *> selectors;
    foreach(CSSInfo * cp, m_styles) {
        selectors.append(cp->getAllSelectors());
    }
    return selectors;
}


CSSInfo::CSSSelector *HTMLStyleInfo::getCSSSelectorForElementClass(const QString &elementName, const QString &className)
{
    foreach(CSSInfo * cp, m_styles) {
        CSSInfo::CSSSelector * cs = cp->getCSSSelectorForElementClass(elementName, className);
        if (cs != NULL) return cs;
    }
    return NULL;    
}


QList<CSSInfo::CSSSelector *> HTMLStyleInfo::getAllCSSSelectorsForElementClass(const QString &elementName, const QString &className)
{
    QList<CSSInfo::CSSSelector *> res;
    foreach(CSSInfo * cp, m_styles) {
        res.append(cp->getAllCSSSelectorsForElementClass(elementName, className));
    }
    return res;    
}


QStringList HTMLStyleInfo::getAllPropertyValues(QString property)
{
    QStringList res;
    foreach(CSSInfo * cp, m_styles) {
        res.append(cp->getAllPropertyValues(property));
    }
    return res;    
}


// not sure how to do this except to do it piece by piece but using
// this will invalidate the CSSInfo pointers stored here
// so disable this for now
QString HTMLStyleInfo::getReformattedCSSText(bool multipleLineFormat)
{
#if 0
    QString csstext(m_source);

    // Note, the EmbeddedPython interface does not handle bool properly
    // So convert to int with 0 or 1 value for the time being
    int useoneline = 1;
    if (multipleLineFormat) useoneline = 0;

    int rv = 0;
    QString error_traceback;
    QList<QVariant> args;
    args.append(QVariant(csstext));
    args.append(QVariant(useoneline));
    EmbeddedPython * epython  = EmbeddedPython::instance();

    QVariant res = epython->runInPython( QString("cssreformatter"),
                                         QString("reformat_css"),
                                         args,
                                         &rv,
                                         error_traceback);
    if (rv != 0) {
        Utility::DisplayStdWarningDialog(QString("Error in cssreformatter: ") + QString::number(rv), 
				         error_traceback);
        // an error happened, return unchanged original
        return QString(csstext);
    }
    // QVariant results are a String List (new_css_text, errors, warnings)
    QStringList results = res.toStringList();
    QString new_csstext = results[0];
    QString errors = results[1];
    QString warnings = results[2];

    if (!errors.isEmpty()) {
        Utility::DisplayStdWarningDialog(QString("Error in cssreformatter: "), errors);
        // an error happened, return unchanged original
        return QString(csstext);
    }

    if (!warnings.isEmpty()) {
        Utility::DisplayStdWarningDialog(QString("Warnings from cssreformatter: "), warnings);
    }

    return new_csstext;
#else
    return m_source;
#endif
}


// Use of this function will invalidate this object completely
QString HTMLStyleInfo::removeMatchingSelectors(QList<CSSInfo::CSSSelector *> cssSelectors)
{
    QStringList style_texts;
    foreach(CSSInfo * cp, m_styles) {
        QString text = cp->removeMatchingSelectors(cssSelectors);
        style_texts << text;
    }
    // now work *backwards* to substitute in each new piece of text
    // while keeping earlier start and length values correct. 
    for(int i = style_texts.length() - 1; i >= 0; i--) {
        QString ntext = style_texts.at(i);
        QString top = m_source.left(m_starts[i]);
        QString bottom = m_source.mid(m_starts[i] + m_lengths[i]);
        m_source = top + ntext + bottom;
    }
    // IMPORTANT: After removing any selectors, users *must*
    // Initialize a new HTMLStyleInfo object to work on the new css text.
    // This HTMLStyleInfo is now obsolete.
    return m_source;
}


// static method used by CodeViewEditor 
QList<HTMLStyleInfo::CSSProperty> HTMLStyleInfo::getCSSProperties(const QString &text, const int &styleTextStartPos, const int &styleTextEndPos)
{
    QList<HTMLStyleInfo::CSSProperty> new_properties;

    if (styleTextEndPos - 1 <= styleTextStartPos) {
        return new_properties;
    }

    const QString &style_text = text.mid(styleTextStartPos, styleTextEndPos - styleTextStartPos);
    QStringList properties = style_text.split(QChar(';'), QString::SkipEmptyParts);
    foreach(QString property_text, properties) {
        if (property_text.trimmed().isEmpty()) {
            continue;
        }

        QStringList name_values = property_text.split(QChar(':'), QString::SkipEmptyParts);
        HTMLStyleInfo::CSSProperty css_property;

        // Any badly formed CSS or stuff we don't "understand" like pre-processing we leave as is
        if (name_values.count() != 2) {
            css_property.name = property_text.trimmed();
            css_property.value = QString();
        } else {
            css_property.name = name_values.at(0).trimmed();
            css_property.value = name_values.at(1).trimmed();
        }

        new_properties.append(css_property);
    }
    return new_properties;
}


// static method used by CodeViewEditory
QString HTMLStyleInfo::formatCSSProperties(QList<HTMLStyleInfo::CSSProperty> new_properties, bool multipleLineFormat, const int &selectorIndent)
{
    QString tab_spaces = QString(" ").repeated(TAB_SPACES_WIDTH + selectorIndent);

    if (new_properties.count() == 0) {
        if (multipleLineFormat) {
            return QString("\n%1")
                   .arg(QString(" ").repeated(selectorIndent));
        } else {
            return QString("");
        }
    } else {
        QStringList property_values;
        foreach(HTMLStyleInfo::CSSProperty new_property, new_properties) {
            if (new_property.value.isNull()) {
                property_values.append(new_property.name);
            } else {
                property_values.append(QString("%1: %2").arg(new_property.name).arg(new_property.value));
            }
        }

        if (multipleLineFormat) {
            return QString("\n%1%2;\n%3")
                   .arg(tab_spaces)
                   .arg(property_values.join(";\n" % tab_spaces))
                   .arg(QString(" ").repeated(selectorIndent));
        } else {
            return QString(" %1; ").arg(property_values.join("; "));
        }
    }
}


bool HTMLStyleInfo::findInlineStyleBlock(const QString &text, const int &offset, int &styleStart, int &styleEnd)
{
    QRegularExpression inline_styles_search("<\\s*style\\s[^>]+>", QRegularExpression::CaseInsensitiveOption|QRegularExpression::InvertedGreedinessOption);
    int style_len = 0;
    styleEnd = -1;
    styleStart = -1;

    QRegularExpressionMatch match = inline_styles_search.match(text, offset);
    if (match.hasMatch()) {
        styleStart = match.capturedStart();
        style_len = match.capturedLength();
    }

    if (styleStart > 0) {
        styleStart += style_len;
        styleEnd = text.indexOf(QRegularExpression("<\\s*/\\s*style\\s*>", QRegularExpression::CaseInsensitiveOption), styleStart);

        if (styleEnd >= styleStart) {
            return true;
        }
    }

    return false;
}
