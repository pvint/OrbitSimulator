// SPDX-License-Identifier: GPL-3.0-or-later
#include "issue_links.hpp"

#include "orbit/core/version.hpp"

#include <QSysInfo>
#include <QUrlQuery>
#include <QtGlobal>

namespace issues {

namespace {

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// GitHub's issue forms take field values from the query: each field's id.
QUrl newIssue(const QString& form, const QUrlQuery& fields) {
    QUrl url(QString::fromLatin1(kRepository) + QStringLiteral("/issues/new"));
    QUrlQuery query(fields);
    query.addQueryItem(QStringLiteral("template"), form);
    url.setQuery(query);
    return url;
}

} // namespace

QString buildDescription() {
    const QString version = toQString(orbit::kVersion);
    const QString build = toQString(orbit::kBuildId);
    return build.isEmpty() ? version : QStringLiteral("%1 (%2)").arg(version, build);
}

QString systemDescription() {
    return QStringLiteral("%1, Qt %2 (%3)")
        .arg(QSysInfo::prettyProductName(), QString::fromLatin1(qVersion()),
             QSysInfo::currentCpuArchitecture());
}

QUrl reportProblemUrl() {
    QUrlQuery fields;
    fields.addQueryItem(QStringLiteral("version"), buildDescription());
    fields.addQueryItem(QStringLiteral("system"), systemDescription());
    return newIssue(QStringLiteral("bug.yml"), fields);
}

QUrl suggestIdeaUrl() { return newIssue(QStringLiteral("idea.yml"), {}); }

} // namespace issues
