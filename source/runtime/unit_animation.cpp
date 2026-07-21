#include "unit_animation.h"

#include <QDataStream>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

QString fixedString(const QByteArray& bytes)
{
    QByteArray value = bytes;
    const int zero = value.indexOf('\0');
    if(zero >= 0) value.truncate(zero);
    return QString::fromLatin1(value).trimmed().toLower();
}

QMap<QString, QByteArray> resMembers(const QByteArray& bytes, QStringList* diagnostics)
{
    QMap<QString, QByteArray> result;
    QDataStream stream(bytes);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, count = 0, tableOffset = 0, namesLength = 0;
    stream >> magic >> count >> tableOffset >> namesLength;
    if(magic != 0x019CE23C || count > 100000 || tableOffset > quint32(bytes.size()))
    {
        if(diagnostics) diagnostics->append("Animation archive has an invalid RES header.");
        return result;
    }
    struct Entry { quint32 size; quint32 offset; quint16 nameLength; quint32 nameOffset; };
    QVector<Entry> entries;
    stream.device()->seek(tableOffset);
    for(quint32 index = 0; index < count; ++index)
    {
        quint32 next = 0, modified = 0;
        Entry entry;
        stream >> next >> entry.size >> entry.offset >> modified >> entry.nameLength >> entry.nameOffset;
        entries.append(entry);
    }
    if(stream.device()->pos() + namesLength > bytes.size())
        return result;
    const QByteArray names = bytes.mid(int(stream.device()->pos()), int(namesLength));
    for(const Entry& entry : entries)
    {
        if(entry.nameOffset + entry.nameLength > quint32(names.size())
                || quint64(entry.offset) + entry.size > quint64(bytes.size()))
            continue;
        const QString name = fixedString(names.mid(int(entry.nameOffset), entry.nameLength));
        result.insert(name, bytes.mid(int(entry.offset), int(entry.size)));
    }
    return result;
}

SAnimationChannel parseChannel(const QString& name, const QByteArray& bytes, QStringList* diagnostics)
{
    SAnimationChannel result;
    result.component = name;
    QDataStream stream(bytes);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 rotations = 0;
    stream >> rotations;
    if(rotations > 10000 || quint64(rotations) * 16 + 8 > quint64(bytes.size()))
    {
        diagnostics->append(name + ": invalid quaternion frame count");
        return result;
    }
    result.rotations.reserve(int(rotations));
    for(quint32 index = 0; index < rotations; ++index)
    {
        float w = 1, x = 0, y = 0, z = 0;
        stream >> w >> x >> y >> z;
        result.rotations.append(QQuaternion(w, x, y, z).normalized());
    }
    quint32 translations = 0;
    stream >> translations;
    if(translations > 10000 || quint64(stream.device()->pos()) + quint64(translations) * 12 > quint64(bytes.size()))
    {
        diagnostics->append(name + ": invalid translation frame count");
        result.rotations.clear();
        return result;
    }
    result.translations.reserve(int(translations));
    for(quint32 index = 0; index < translations; ++index)
    {
        float x = 0, y = 0, z = 0;
        stream >> x >> y >> z;
        result.translations.append(QVector3D(x, y, z));
    }
    if(quint64(stream.device()->pos()) + 4 <= quint64(bytes.size()))
    {
        quint32 morphFrames = 0;
        stream >> morphFrames;
        if(morphFrames > 10000)
        {
            diagnostics->append(name + ": invalid morph frame count");
            return result;
        }
        if(morphFrames > 0)
        {
            quint32 morphVertices = 0;
            stream >> morphVertices;
            const quint64 values = quint64(morphFrames) * quint64(morphVertices);
            if(morphVertices > 100000 || values > 10000000
                    || quint64(stream.device()->pos()) + values * 12 > quint64(bytes.size()))
            {
                diagnostics->append(name + ": invalid morph vertex payload");
                return result;
            }
            result.morphations.resize(int(morphFrames));
            for(quint32 frame = 0; frame < morphFrames; ++frame)
            {
                result.morphations[int(frame)].reserve(int(morphVertices));
                for(quint32 vertex = 0; vertex < morphVertices; ++vertex)
                {
                    float x = 0, y = 0, z = 0;
                    stream >> x >> y >> z;
                    result.morphations[int(frame)].append(QVector3D(x, y, z));
                }
            }
        }
    }
    return result;
}

QString canonicalChannel(QString component)
{
    component = component.toLower();
    static const QRegularExpression numbered("(\\.(?:armor|sword|axe|dagger|pike|club|bow|crossbow|shield|dsword|daxe|dpike|dclub|staff|dstaff))\\d+$");
    const QRegularExpressionMatch match = numbered.match(component);
    if(match.hasMatch())
        component.replace(match.capturedStart(0), match.capturedLength(0), match.captured(1) + "00");
    return component;
}

bool inheritsCanonicalChannel(const QString& component, const QString& canonical,
                              const SAnimationSkeleton& skeleton)
{
    QString ancestor = skeleton.parents.value(component);
    QSet<QString> visited;
    while(!ancestor.isEmpty() && !visited.contains(ancestor))
    {
        if(ancestor == canonical)
            return true;
        visited.insert(ancestor);
        ancestor = skeleton.parents.value(ancestor);
    }
    return false;
}

QMatrix4x4 localAt(const SAnimationClip& clip, const QString& component, double frame)
{
    auto found = clip.channels.constFind(component.toLower());
    if(found == clip.channels.constEnd())
        found = clip.channels.constFind(canonicalChannel(component));
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    if(found == clip.channels.constEnd())
        return matrix;
    const SAnimationChannel& channel = found.value();
    const int length = qMax(1, clip.logicalLength);
    const int base = int(qFloor(frame)) % length;
    const int next = (base + 1) % length;
    const float alpha = float(frame - qFloor(frame));
    QVector3D translation;
    if(!channel.translations.isEmpty())
    {
        const int a = qMin(base, channel.translations.size() - 1);
        const int b = channel.translations.size() > length
            ? qMin(base + 1, channel.translations.size() - 1)
            : qMin(next, channel.translations.size() - 1);
        translation = channel.translations[a] * (1.0f - alpha) + channel.translations[b] * alpha
            - channel.translations.first();
    }
    QQuaternion rotation;
    if(!channel.rotations.isEmpty())
    {
        const int a = qMin(base, channel.rotations.size() - 1);
        const int b = channel.rotations.size() > length
            ? qMin(base + 1, channel.rotations.size() - 1)
            : qMin(next, channel.rotations.size() - 1);
        rotation = QQuaternion::slerp(channel.rotations[a], channel.rotations[b], alpha).normalized();
    }
    matrix.translate(translation);
    matrix.rotate(rotation);
    return matrix;
}

QVector<QVector3D> morphAt(const SAnimationClip& clip, const SAnimationChannel& channel,
                           double frame)
{
    QVector<QVector3D> result;
    if(channel.morphations.isEmpty())
        return result;
    const int length = qMax(1, clip.logicalLength);
    const int base = int(qFloor(frame)) % length;
    const int next = (base + 1) % length;
    const float alpha = float(frame - qFloor(frame));
    const int a = qMin(base, channel.morphations.size() - 1);
    const int b = channel.morphations.size() > length
        ? qMin(base + 1, channel.morphations.size() - 1)
        : qMin(next, channel.morphations.size() - 1);
    const int vertices = qMin(channel.morphations[a].size(), channel.morphations[b].size());
    result.reserve(vertices);
    for(int vertex = 0; vertex < vertices; ++vertex)
        result.append(channel.morphations[a][vertex] * (1.0f - alpha)
                      + channel.morphations[b][vertex] * alpha);
    return result;
}

}

QVector<SAdbAction> parseAdbActions(const QByteArray& bytes, QStringList* diagnostics)
{
    QVector<SAdbAction> result;
    if(bytes.size() < 44 || bytes.left(4) != QByteArray("ADB\0", 4))
    {
        if(diagnostics) diagnostics->append("ADB header is missing or invalid.");
        return result;
    }
    QDataStream stream(bytes);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream.skipRawData(4);
    quint32 count = 0;
    stream >> count;
    if(count > 10000)
    {
        if(diagnostics) diagnostics->append("ADB action count exceeds the safety bound.");
        return result;
    }
    stream.skipRawData(24 + 12);
    const int recordSize = 16 + 4 + 8 + 8 + 4 + 48;
    if(quint64(stream.device()->pos()) + quint64(count) * recordSize > quint64(bytes.size()))
    {
        if(diagnostics) diagnostics->append("ADB action table is truncated.");
        return result;
    }
    result.reserve(int(count));
    for(quint32 index = 0; index < count; ++index)
    {
        SAdbAction action;
        action.name = fixedString(bytes.mid(int(stream.device()->pos()), 16));
        stream.skipRawData(16);
        quint64 packed = 0;
        stream >> action.number >> packed;
        action.weaponMask = quint16(packed & 0x7fff);
        action.allowedState = quint8((packed >> 15) & 7);
        action.actionType = quint8((packed >> 18) & 15);
        action.modifier = quint8((packed >> 22) & 255);
        action.stage = quint8((packed >> 30) & 3);
        stream >> action.probability >> action.length;
        float movement = 0;
        stream >> movement;
        stream.skipRawData(48);
        result.append(action);
    }
    return result;
}

bool selectDefaultIdleAction(const QVector<SAdbAction>& actions, int weaponTypeId,
                             bool allowExtendedWeaponMask, SAdbAction* selected,
                             QString* diagnostic)
{
    if(!selected) return false;
    QVector<SAdbAction> candidates;
    for(const SAdbAction& action : actions)
    {
        if(action.actionType != 6 || action.stage != 2 || action.length == 0)
            continue;
        if(!allowExtendedWeaponMask && (action.weaponMask & ~quint16(0x7f)))
            continue;
        const bool weaponCompatible = weaponTypeId < 0 ? action.weaponMask == 0
            : (weaponTypeId < 15 && (action.weaponMask & (quint16(1) << weaponTypeId)));
        if(action.weaponMask != 0 && !weaponCompatible)
            continue;
        candidates.append(action);
    }
    if(candidates.isEmpty())
    {
        if(diagnostic) *diagnostic = "No compatible IDLE cycle exists in the selected provider ADB.";
        return false;
    }
    std::sort(candidates.begin(), candidates.end(), [](const SAdbAction& a, const SAdbAction& b)
    {
        const int aState = a.allowedState == 0 ? 0 : (a.allowedState == 1 ? 1 : (a.allowedState == 2 ? 2 : 3));
        const int bState = b.allowedState == 0 ? 0 : (b.allowedState == 1 ? 1 : (b.allowedState == 2 ? 2 : 3));
        if(aState != bState) return aState < bState;
        if(a.probability != b.probability) return a.probability > b.probability;
        return a.number < b.number;
    });
    *selected = candidates.first();
    return true;
}

bool selectJmvDefaultIdleAction(const QVector<SAdbAction>& actions, const QString& model,
                                int weaponTypeId, SAdbAction* selected, QString* diagnostic)
{
    if(!selected) return false;
    QVector<SAdbAction> standing;
    for(const SAdbAction& action : actions)
        if(action.actionType == 6 && action.stage == 2 && action.length > 0
                && action.allowedState == 2)
            standing.append(action);
    if(standing.isEmpty())
    {
        if(diagnostic) *diagnostic = "JMV ADB has no standing IDLE cycle (allowed state 2).";
        return false;
    }

    QStringList preferredNames;
    const QString normalizedModel = model.trimmed().toLower();
    if((normalizedModel == "unhuma" || normalizedModel == "unhufe"
            || normalizedModel == "unorma" || normalizedModel == "unorfe"
            || normalizedModel == "unhusk") && weaponTypeId >= 10 && weaponTypeId <= 15)
        preferredNames << ((weaponTypeId == 13 || weaponTypeId == 14)
                              ? "2hacidle01" : "2hscidle01");
    preferredNames << "cidle01" << "cidle" << "cidle02";

    auto deterministicBest = [](QVector<SAdbAction> candidates) -> SAdbAction
    {
        std::sort(candidates.begin(), candidates.end(), [](const SAdbAction& a, const SAdbAction& b)
        {
            if(a.probability != b.probability) return a.probability > b.probability;
            return a.number < b.number;
        });
        return candidates.first();
    };
    for(const QString& name : preferredNames)
    {
        QVector<SAdbAction> named;
        for(const SAdbAction& action : standing)
            if(action.name.compare(name, Qt::CaseInsensitive) == 0)
                named.append(action);
        if(!named.isEmpty())
        {
            *selected = deterministicBest(named);
            return true;
        }
    }

    QVector<SAdbAction> compatible;
    for(const SAdbAction& action : standing)
    {
        // JMV action tables commonly use a non-zero aggregate mask even for
        // unarmed cycles (unmogo cidle uses 127), so unarmed cannot mean mask 0.
        if(weaponTypeId < 0 || action.weaponMask == 0
                || (weaponTypeId < 15 && (action.weaponMask & (quint16(1) << weaponTypeId))))
            compatible.append(action);
    }
    if(compatible.isEmpty()) compatible = standing;
    *selected = deterministicBest(compatible);
    return true;
}

SAnimationClip parseNestedAnimationClip(const QByteArray& modelAnimationArchive,
                                        const QString& actionName, int logicalLength)
{
    SAnimationClip result;
    result.actionName = actionName.toLower();
    result.logicalLength = logicalLength;
    const QMap<QString, QByteArray> actions = resMembers(modelAnimationArchive, &result.diagnostics);
    const QByteArray action = actions.value(result.actionName);
    if(action.isEmpty())
    {
        result.diagnostics.append("Model ANM has no selected action member: " + result.actionName);
        return result;
    }
    const QMap<QString, QByteArray> components = resMembers(action, &result.diagnostics);
    for(auto it = components.constBegin(); it != components.constEnd(); ++it)
    {
        SAnimationChannel channel = parseChannel(it.key(), it.value(), &result.diagnostics);
        if(!channel.rotations.isEmpty() || !channel.translations.isEmpty()
                || !channel.morphations.isEmpty())
            result.channels.insert(it.key().toLower(), channel);
    }
    return result;
}

SAnimationLocalPose interpolateAnimationPose(const SAnimationClip& clip, double logicalFrame,
                                             const SAnimationSkeleton* skeleton)
{
    SAnimationLocalPose result;
    for(auto it = clip.channels.constBegin(); it != clip.channels.constEnd(); ++it)
    {
        if(skeleton && !skeleton->restOffsets.contains(it.key()))
            continue;
        result.transforms.insert(it.key(), localAt(clip, it.key(), logicalFrame));
        const QVector<QVector3D> morph = morphAt(clip, it.value(), logicalFrame);
        if(!morph.isEmpty()) result.morphDeltas.insert(it.key(), morph);
    }
    return result;
}

QMap<QString, QMatrix4x4> propagateAnimationHierarchy(
    const SAnimationLocalPose& pose, const SAnimationSkeleton& skeleton)
{
    QMap<QString, QMatrix4x4> current;
    QMap<QString, QQuaternion> absoluteRotations;
    QMap<QString, QVector3D> currentPositions;
    QSet<QString> resolving;
    std::function<void(const QString&)> resolve = [&](const QString& component)
    {
        if(current.contains(component)) return;
        if(resolving.contains(component))
        {
            QMatrix4x4 identity;
            identity.setToIdentity();
            current.insert(component, identity);
            return;
        }
        resolving.insert(component);
        const QString parent = skeleton.parents.value(component);
        const bool hasParent = !parent.isEmpty() && skeleton.restOffsets.contains(parent);
        if(hasParent) resolve(parent);
        const QVector3D rest = skeleton.restOffsets.value(component);
        auto channel = pose.transforms.constFind(component);
        if(channel == pose.transforms.constEnd())
        {
            const QString canonical = canonicalChannel(component);
            // Numbered equipment is linked beneath its animated *00 attachment
            // node in the shipped assemblies (for example
            // lh2.shield04 -> lh2.shield00 -> lh2). The child inherits that
            // node's transform through hierarchy propagation; applying the
            // canonical channel again rotates the visible item twice.
            if(!inheritsCanonicalChannel(component, canonical, skeleton))
                channel = pose.transforms.constFind(canonical);
        }
        const bool animated = channel != pose.transforms.constEnd();
        const QQuaternion localRotation = animated
            ? QQuaternion::fromRotationMatrix(channel.value().normalMatrix()).normalized()
            : QQuaternion();
        // ei_figer establishes that EI child rotations pre-multiply the
        // parent's absolute rotation. Reversing this order makes articulated
        // parts orbit away from their joints and rotates attached weapons in
        // the wrong basis.
        const QQuaternion absoluteRotation = hasParent
            ? (localRotation * absoluteRotations.value(parent)).normalized()
            : localRotation;
        QVector3D position = rest;
        if(hasParent)
            position = currentPositions.value(parent)
                + absoluteRotations.value(parent).rotatedVector(rest - skeleton.restOffsets.value(parent));
        else if(animated)
            position = rest + channel.value().column(3).toVector3D();

        QMatrix4x4 animatedGlobal;
        animatedGlobal.setToIdentity();
        animatedGlobal.translate(position);
        animatedGlobal.rotate(absoluteRotation);
        QMatrix4x4 inverseBind;
        inverseBind.setToIdentity();
        inverseBind.translate(-rest);
        current.insert(component, animatedGlobal * inverseBind);
        absoluteRotations.insert(component, absoluteRotation);
        currentPositions.insert(component, position);
        resolving.remove(component);
    };
    for(auto it = skeleton.restOffsets.constBegin(); it != skeleton.restOffsets.constEnd(); ++it)
        resolve(it.key());
    return current;
}

EAnimationDistanceTier animationDistanceTier(bool visible, bool selected, float distance)
{
    if(!visible) return EAnimationDistanceTier::Skip;
    if(selected || distance <= 100.0f) return EAnimationDistanceTier::Full;
    if(distance <= 300.0f) return EAnimationDistanceTier::HalfRate;
    return EAnimationDistanceTier::LowRate;
}

double quantizedAnimationFrame(EAnimationDistanceTier tier, qint64 elapsedMilliseconds,
                               int logicalLength, quint32 phaseSeed)
{
    if(logicalLength <= 0 || tier == EAnimationDistanceTier::Skip)
        return 0.0;
    double frame = (double(elapsedMilliseconds) * 15.0 / 1000.0) + double(phaseSeed % quint32(logicalLength));
    frame = std::fmod(frame, double(logicalLength));
    if(tier == EAnimationDistanceTier::HalfRate)
        frame = qFloor(frame * 0.5) * 2.0;
    else if(tier == EAnimationDistanceTier::LowRate)
        frame = qFloor(frame * 0.25) * 4.0;
    return frame;
}

AnimationFrameContext& AnimationFrameContext::instance()
{
    static AnimationFrameContext context;
    return context;
}

void AnimationFrameContext::beginFrame(const QVector3D& cameraPosition,
                                       qint64 elapsedMilliseconds, bool enabled)
{
    m_cameraPosition = cameraPosition;
    m_elapsedMilliseconds = elapsedMilliseconds;
    m_enabled = enabled;
    m_counters = SAnimationPerformanceCounters();
}
