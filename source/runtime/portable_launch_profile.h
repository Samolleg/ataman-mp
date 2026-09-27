#pragma once

#include <QFile>
#include <QDir>
#include <QMap>
#include <QSet>
#include <QDataStream>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QRegularExpression>

// The portable profile is input, never a registry import. Only this run's host
// consumes the bounded binary snapshot; no installed settings are changed.
namespace PortableLaunchProfile {
struct Value { QString section, name; quint32 kind; QByteArray bytes; };
inline QByteArray wide(const QString& value) {
    QByteArray result;
    QDataStream out(&result, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    for(QChar c : value) out << quint16(c.unicode());
    out << quint16(0);
    return result;
}
inline bool read(const QString& path, QMap<QString, Value>& values, QString* error) {
    QFile file(path);
    auto fail = [&]() { if(error) *error = QString("Invalid portable profile: %1").arg(path); return false; };
    if(!file.open(QIODevice::ReadOnly) || file.size() > 4*1024*1024) return fail();
    QXmlStreamReader xml(&file);
    if(!xml.readNextStartElement() || xml.name() != "profile" || xml.attributes().value("version") != "1") return fail();
    QSet<QString> seen;
    while(xml.readNextStartElement()) {
        if(xml.name() != "value") return fail();
        Value v;
        v.section = xml.attributes().value("section").toString();
        v.name = xml.attributes().value("name").toString();
        bool ok = false;
        v.kind = xml.attributes().value("kind").toUInt(&ok);
        const QString section = v.section.toLower();
        if(!ok || !xml.attributes().hasAttribute("name") || v.name.contains(QChar(0))
           || v.name.size() > 1024 || v.section.size() > 1024
           || v.section.contains('/') || v.section.contains(QChar(0))
           || v.section.split('\\').contains("..")
           || !(section == "settings" || section.startsWith("settings\\")
                || section == "evilislands" || section.startsWith("evilislands\\"))) return fail();
        const QString id = section + QChar(0) + v.name.toLower();
        if(seen.contains(id) || seen.size() >= 10000) return fail();
        seen.insert(id);
        const QString text = xml.readElementText();
        if(xml.hasError() || text.size() > 1024*1024) return fail();
        QDataStream out(&v.bytes, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        switch(v.kind) {
        case 1: case 2: v.bytes = wide(text); break;
        case 4: {
            qint64 n = text.toLongLong(&ok);
            if(!ok || n < -2147483648LL || n > 4294967295LL) return fail();
            out << quint32(n); break;
        }
        case 11: { const qint64 n = text.toLongLong(&ok); if(!ok) return fail(); out << quint64(n); break; }
        case 0: case 3: case 7: {
            const QByteArray encoded = text.toLatin1();
            v.bytes = QByteArray::fromBase64(encoded);
            if(v.bytes.toBase64() != encoded) return fail();
            if(v.kind == 7) {
                if(v.bytes.size()%2) return fail();
                v.bytes.append(QByteArray(v.bytes.isEmpty() ? 4 : 2, '\0'));
            }
            break;
        }
        default: return fail();
        }
        values[id] = v;
    }
    while(!xml.atEnd()) xml.readNext();
    return !xml.hasError() || fail();
}
inline bool write(const QString& root, const QString& mod, const QString& destination, QString* error) {
    QMap<QString, Value> values;
    if(!read(QDir(root).filePath("portable-defaults.xml"), values, error)) return false;
    const QString saved = QDir(root).filePath("portable-settings.xml");
    if(QFile::exists(saved) && !read(saved, values, error)) return false;
    const QString canonicalMod = QFileInfo(mod).canonicalFilePath();
    const QString mods = QFileInfo(QDir(root).filePath("MODS")).canonicalFilePath();
    if(mods.isEmpty() || !canonicalMod.startsWith(mods + '/', Qt::CaseInsensitive)) {
        if(error) *error = "Portable mod must be inside the selected MODS directory.";
        return false;
    }
    auto put = [&](const QString& section, const QString& name, const QString& data) {
        Value v{section, name, 1, wide(data)};
        values[section.toLower() + QChar(0) + name.toLower()] = v;
    };
    auto path = [](const QString& p) { return QDir::toNativeSeparators(QDir::cleanPath(p)) + '\\'; };
    put("", "Path", path(root));
    put("", "AddonPath", QDir::toNativeSeparators(QDir(mod).filePath("addon.dll")));
    put("Settings", "AddonPath", path(mod));
    put("EvilIslands\\Path Settings", "Work Path", path(QDir(root).filePath("Base")));
    put("EvilIslands\\Path Settings", "CD-ROM Path", path(QDir(root).filePath("Base")));
    put("EvilIslands\\Path Settings", "Resources Path", path(QDir(root).filePath("Base/Res")));
    // Test runs must not inherit a saved campaign auto-command.
    Value autoexec{"Settings", "Console AutoExec", 7, QByteArray(4, '\0')};
    values[QString("settings") + QChar(0) + "console autoexec"] = autoexec;
    const QString resolutionId = QString("evilislands\\general settings") + QChar(0) + "videoresolution";
    if(!values.contains(resolutionId)) put("EvilIslands\\General Settings", "VIDEORESOLUTION", "800x600");
    QSaveFile file(destination);
    if(!file.open(QIODevice::WriteOnly)) { if(error) *error = file.errorString(); return false; }
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    out << quint32(0x31525045) << quint32(values.size());
    for(const Value& v : values) {
        const QByteArray section = wide(v.section), name = wide(v.name);
        out << quint32(section.size()) << quint32(name.size()) << v.kind << quint32(v.bytes.size());
        out.writeRawData(section.constData(), section.size());
        out.writeRawData(name.constData(), name.size());
        out.writeRawData(v.bytes.constData(), v.bytes.size());
    }
    if(out.status() != QDataStream::Ok || !file.commit()) { if(error) *error = file.errorString(); return false; }
    return true;
}
}
