// SPDX-License-Identifier: GPL-2.0-only
#include "catalog.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QByteArrayView>
#include <QSet>

void Catalog::parse(Profile &profile, const QByteArray &data) {
    auto record = [this, &profile](QByteArrayView name, QByteArrayView raw) {
        if (name.isEmpty()) return;
        for (char c : name) if (!(c == '_' || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return;
        auto key = QString::fromUtf8(name);
        auto it = symbolIds.constFind(key);
        int id = it == symbolIds.cend() ? symbolIds.size() : it.value();
        if (it == symbolIds.cend()) symbolIds.insert(key, id);
        if (id >= profile.states.size()) profile.states.append(QByteArray(id + 1 - profile.states.size(), 0));
        char state = raw == "n" ? 1 : raw == "m" ? 2 : raw == "y" ? 3 : 4;
        profile.states[id] = state;
        if (state == 4) {
            auto value = QString::fromUtf8(raw);
            auto scalar = scalarValues.constFind(value);
            if (scalar == scalarValues.cend()) scalar = scalarValues.insert(value, value);
            profile.scalars.insert(id, scalar.value());
        } else profile.scalars.remove(id);
    };
    profile.states = QByteArray(symbolIds.size(), 0);
    QByteArrayView input(data);
    while (!input.isEmpty()) {
        auto end = input.indexOf('\n');
        auto line = end < 0 ? input : input.first(end);
        input = end < 0 ? QByteArrayView{} : input.sliced(end + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith("CONFIG_")) {
            auto equal = line.indexOf('=');
            if (equal > 7) record(line.sliced(7, equal - 7), line.sliced(equal + 1));
        } else if (line.startsWith("# CONFIG_") && line.endsWith(" is not set")) {
            record(line.sliced(9, line.size() - 20), "n");
        }
    }
    profile.states.squeeze();
}

QString Catalog::value(const Profile &profile, int id, const QString &missing) const {
    const int state = id >= 0 && id < profile.states.size() ? profile.states[id] : 0;
    switch (state) {
    case 1: return QStringLiteral("n");
    case 2: return QStringLiteral("m");
    case 3: return QStringLiteral("y");
    case 4: return profile.scalars.value(id);
    default: return missing;
    }
}
QString Catalog::value(const Profile &profile, const QString &symbol, const QString &missing) const {
    return value(profile, symbolIds.value(symbol, -1), missing);
}

bool Catalog::load(const QString &path, const QString &assets, QString &error, const QString &architecture) {
    Q_UNUSED(assets);
    directory = path;
    QFile file(path + "/index.json");
    if (!file.open(QIODevice::ReadOnly)) { error = "No catalog yet. Run python3 tools/collect.py."; return false; }
    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || doc[QStringLiteral("schema")].toInt() != 1) {
        error = "Invalid or unsupported catalog/index.json"; return false;
    }
    profiles.clear(); distros.clear(); patches = doc[QStringLiteral("patches")].toArray(); symbolIds.clear(); scalarValues.clear(); patchMatches.clear();
    for (auto entry : doc[QStringLiteral("configs")].toArray()) {
        if (!architecture.isEmpty() && entry[QStringLiteral("arch")].toString() != architecture) continue;
        Profile profile;
        profile.metadata = entry.toObject();
        profile.id = entry[QStringLiteral("id")].toString(); profile.distro = entry[QStringLiteral("distro")].toString();
        profile.label = entry[QStringLiteral("release")].toString() + " · " + entry[QStringLiteral("flavor")].toString();
        profile.file = QDir(path).absoluteFilePath(entry[QStringLiteral("path")].toString());
        if (!QDir::cleanPath(profile.file).startsWith(QDir(path).absolutePath() + "/")) {
            error = "Config path leaves the catalog: " + profile.id; return false;
        }
        QFile config(profile.file);
        if (!config.open(QIODevice::ReadOnly)) { error = "Missing config: " + profile.id; return false; }
        auto bytes = config.readAll();
        if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != entry[QStringLiteral("sha256")].toString().toUtf8()) {
            error = "Config checksum mismatch: " + profile.id; return false;
        }
        parse(profile, bytes); profiles.append(std::move(profile));
    }
    const QList<QPair<QString, QString>> names = {{"ubuntu", "Ubuntu"}, {"arch", "Arch"}, {"debian", "Debian"},
        {"fedora", "Fedora"}, {"nobara", "Nobara"}, {"nixos", "NixOS"}, {"gentoo", "Gentoo"},
        {"opensuse", "openSUSE"}, {"cachyos", "CachyOS"}, {"steamos", "SteamOS"}, {"alpine", "Alpine"}, {"popos", "Pop!_OS"}};
    for (auto name : names) {
        bool present = false;
        for (const auto &profile : profiles) if (profile.distro == name.first) present = true;
        if (present) distros.append({name.first, name.second});
    }
    for (const auto &profile : profiles) {
        bool present = false;
        for (const auto &distro : distros) present |= distro.id == profile.distro;
        if (!present) {
            QString label = profile.distro; label[0] = label[0].toUpper();
            distros.append({profile.distro, label});
        }
    }
    return true;
}

QJsonArray Catalog::compare(const QString &symbol) const {
    QJsonArray rows;
    const int id = symbolIds.value(symbol, -1);
    for (const auto &profile : profiles) {
        auto row = profile.metadata;
        row[QStringLiteral("value")] = id >= 0 && id < profile.states.size() && profile.states[id] ? QJsonValue(value(profile, id)) : QJsonValue(QJsonValue::Null);
        rows.append(row);
    }
    return rows;
}
int Catalog::supportCount(const QString &symbol) const {
    QSet<QString> supported;
    const int id = symbolIds.value(symbol, -1);
    for (const auto &profile : profiles) {
        auto val = value(profile, id);
        if (val == "y" || val == "m") supported.insert(profile.distro);
    }
    return supported.size();
}
QString Catalog::tooltip(const QString &symbol) const {
    QString text = QString::number(supportCount(symbol)) + " distros enable CONFIG_" + symbol + "\n";
    for (const auto &profile : profiles) text += "\n" + profile.distro + " " + profile.label + ": " + value(profile, symbol, "not recorded");
    return text;
}
namespace {
// The searchable tokens are Kconfig identifiers (ASCII). Keep a bounded overlap
// so a token or file path spanning two chunks has the same match as a full diff.
bool word(char c) {
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
bool mentions(const QByteArray &text, const QByteArray &token, bool eof, bool continued) {
    qsizetype pos = 0;
    while ((pos = text.indexOf(token, pos)) >= 0) {
        const auto end = pos + token.size();
        if ((pos == 0 ? !continued : !word(text[pos - 1])) &&
            (end < text.size() ? !word(text[end]) : eof)) return true;
        ++pos;
    }
    return false;
}
}
QJsonArray Catalog::patchesFor(const QString &symbol, const QString &path) const {
    QJsonArray found;
    if (symbol.isEmpty()) return found;
    const auto key = symbol + '\n' + path;
    auto *cached = patchMatches.object(key);
    QVector<PatchMatch> matches;
    if (cached) matches = *cached;
    else {
        const auto bare = symbol.toUtf8(), config = "CONFIG_" + bare;
        const auto fileRef = path.isEmpty() ? QByteArray() : "b/" + path.toUtf8();
        const auto overlap = qMax(config.size(), fileRef.size()) + 2;
        for (int i = 0; i < patches.size(); ++i) {
            const auto absolute = QDir(directory).absoluteFilePath(patches[i][QStringLiteral("path")].toString());
            if (!QDir::cleanPath(absolute).startsWith(QDir(directory).absolutePath() + "/")) continue;
            QFile patch(absolute);
            if (!patch.open(QIODevice::ReadOnly)) continue;
            QByteArray tail; bool symbolMatch = false, fileMatch = false;
            while (!patch.atEnd()) {
                const auto next = patch.read(64 * 1024);
                if (next.isEmpty()) break;
                const auto chunk = tail + next;
                symbolMatch = mentions(chunk, bare, patch.atEnd(), !tail.isEmpty()) || mentions(chunk, config, patch.atEnd(), !tail.isEmpty());
                if (symbolMatch) break;
                fileMatch |= !fileRef.isEmpty() && chunk.contains(fileRef);
                tail = chunk.right(overlap);
                // A continued chunk starts inside the previous read. Ignore its
                // artificial leading boundary; all spanning tokens have a
                // predecessor because the overlap is longer than any token.
            }
            if (symbolMatch || fileMatch) matches.append({i, symbolMatch});
        }
        patchMatches.insert(key, new QVector<PatchMatch>(matches),
                            qMax(1, int(matches.size() * sizeof(PatchMatch) + key.size() * 2 + 128)));
    }
    for (const auto &match : matches) {
        auto record = patches[match.index].toObject();
        record[QStringLiteral("match")] = match.symbol ? "mentions symbol" : "touches Kconfig file";
        found.append(record);
    }
    return found;
}
