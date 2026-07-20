#include "terrain_atlas_analyzer.h"

#include <QSet>

#include <algorithm>
#include <cmath>

#include "endurance_document.h"
#include "mpr_document.h"

namespace {
struct SVisualFeature
{
    double value[6]{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

struct SPureCandidate
{
    int slot{-1};
    qint32 surfaceType{0};
    SVisualFeature feature;
};

struct SMaterialCluster
{
    qint32 surfaceType{0};
    int representativeSlot{-1};
    QVector<SVisualFeature> members;
    SVisualFeature centroid;
};

double featureDistance(const SVisualFeature& left, const SVisualFeature& right)
{
    double sum = 0.0;
    for(int component = 0; component < 6; ++component)
    {
        const double delta = left.value[component] - right.value[component];
        sum += delta * delta;
    }
    return std::sqrt(sum / 6.0);
}

SVisualFeature averageFeatures(const QVector<SVisualFeature>& features)
{
    SVisualFeature result;
    if(features.isEmpty())
        return result;
    for(const SVisualFeature& feature : features)
        for(int component = 0; component < 6; ++component)
            result.value[component] += feature.value[component];
    for(double& component : result.value)
        component /= double(features.size());
    return result;
}

SVisualFeature cornerFeature(const QImage& source, int corner)
{
    const QImage& image = source;
    SVisualFeature result;
    if(image.isNull())
        return result;
    const int marginX = qMax(1, image.width() / 16);
    const int marginY = qMax(1, image.height() / 16);
    const int spanX = qMax(4, image.width() / 5);
    const int spanY = qMax(4, image.height() / 5);
    const bool east = corner == 1 || corner == 2;
    const bool south = corner >= 2;
    const int startX = east ? qMax(marginX, image.width() - marginX - spanX) : marginX;
    const int startY = south ? qMax(marginY, image.height() - marginY - spanY) : marginY;
    const int endX = qMin(image.width() - marginX, startX + spanX);
    const int endY = qMin(image.height() - marginY, startY + spanY);
    const int stepX = qMax(1, (endX - startX) / 10);
    const int stepY = qMax(1, (endY - startY) / 10);
    double sum[3]{0.0, 0.0, 0.0};
    double square[3]{0.0, 0.0, 0.0};
    int samples = 0;
    for(int y = startY; y < endY; y += stepY)
    {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for(int x = startX; x < endX; x += stepX)
        {
            const double channels[3]{double(qRed(row[x])), double(qGreen(row[x])),
                                     double(qBlue(row[x]))};
            for(int channel = 0; channel < 3; ++channel)
            {
                sum[channel] += channels[channel];
                square[channel] += channels[channel] * channels[channel];
            }
            ++samples;
        }
    }
    if(samples == 0)
        return result;
    for(int channel = 0; channel < 3; ++channel)
    {
        const double mean = sum[channel] / samples;
        result.value[channel] = mean;
        result.value[channel + 3] = std::sqrt(qMax(0.0, square[channel] / samples - mean * mean));
    }
    return result;
}

bool isPureTile(const QVector<SVisualFeature>& corners)
{
    const double kPureCornerDistance = 38.0;
    for(int left = 0; left < corners.size(); ++left)
        for(int right = left + 1; right < corners.size(); ++right)
            if(featureDistance(corners.at(left), corners.at(right)) > kPureCornerDistance)
                return false;
    return true;
}

int nearestCluster(const SVisualFeature& feature,
                   const QVector<SMaterialCluster>& clusters,
                   bool* confident)
{
    int bestIndex = -1;
    double best = 1.0e30;
    double second = 1.0e30;
    for(int index = 0; index < clusters.size(); ++index)
    {
        const double distance = featureDistance(feature, clusters.at(index).centroid);
        if(distance < best)
        {
            second = best;
            best = distance;
            bestIndex = index;
        }
        else if(distance < second)
            second = distance;
    }
    // Strong matches do not need a wide runner-up margin. Less certain matches
    // are accepted only when the next material is visibly farther away.
    *confident = bestIndex >= 0 && best <= 68.0
        && (best <= 24.0 || second - best >= 7.0);
    return bestIndex;
}
}

STerrainAtlasAnalysis CTerrainAtlasAnalyzer::analyze(
    const QVector<QImage>& images, const QVector<qint32>& surfaceTypes,
    CValidationReport* report)
{
    STerrainAtlasAnalysis result;
    QVector<QVector<SVisualFeature>> features(images.size());
    QVector<SPureCandidate> pure;
    for(int slot = 0; slot < images.size(); ++slot)
    {
        if(images.at(slot).isNull())
            continue;
        ++result.analyzedSlots;
        const QImage normalized = images.at(slot).convertToFormat(QImage::Format_RGB32);
        for(int corner = 0; corner < 4; ++corner)
            features[slot].append(cornerFeature(normalized, corner));
        if(isPureTile(features.at(slot)))
        {
            SPureCandidate candidate;
            candidate.slot = slot;
            candidate.surfaceType = slot < surfaceTypes.size() ? surfaceTypes.at(slot) : 0;
            candidate.feature = averageFeatures(features.at(slot));
            pure.append(candidate);
        }
    }

    const double kNaturalVariantDistance = 30.0;
    QVector<SMaterialCluster> clusters;
    for(const SPureCandidate& candidate : pure)
    {
        int bestCluster = -1;
        double bestDistance = 1.0e30;
        for(int index = 0; index < clusters.size(); ++index)
        {
            if(clusters.at(index).surfaceType != candidate.surfaceType)
                continue;
            const double distance = featureDistance(candidate.feature, clusters.at(index).centroid);
            if(distance < bestDistance)
            {
                bestDistance = distance;
                bestCluster = index;
            }
        }
        if(bestCluster < 0 || bestDistance > kNaturalVariantDistance)
        {
            SMaterialCluster cluster;
            cluster.surfaceType = candidate.surfaceType;
            cluster.representativeSlot = candidate.slot;
            cluster.members.append(candidate.feature);
            cluster.centroid = candidate.feature;
            clusters.append(cluster);
        }
        else
        {
            clusters[bestCluster].members.append(candidate.feature);
            clusters[bestCluster].centroid = averageFeatures(clusters.at(bestCluster).members);
        }
    }

    for(int index = 0; index < clusters.size(); ++index)
    {
        STerrainAtlasMaterial material;
        material.id = index + 1;
        material.surfaceType = clusters.at(index).surfaceType;
        material.representativeSlot = clusters.at(index).representativeSlot;
        material.pureVariantCount = clusters.at(index).members.size();
        result.materials.append(material);
    }

    for(int slot = 0; slot < features.size(); ++slot)
    {
        if(features.at(slot).size() != 4)
            continue;
        STerrainTransitionSignature signature;
        qint32* target[4]{&signature.northWest, &signature.northEast,
                         &signature.southEast, &signature.southWest};
        bool described = true;
        for(int corner = 0; corner < 4; ++corner)
        {
            bool confident = false;
            const int cluster = nearestCluster(features.at(slot).at(corner), clusters, &confident);
            if(!confident)
            {
                described = false;
                break;
            }
            *target[corner] = cluster + 1;
        }
        if(!described)
        {
            ++result.ambiguousSlots;
            continue;
        }
        bool added = true;
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.atlasIndex = quint8(slot / 64);
            packed.tileIndex = quint8(slot % 64);
            packed.rotation = quint8(rotation);
            STerrainTransitionVariant variant;
            variant.signature = CTerrainTransitionCatalog::rotated(signature, rotation);
            variant.packedTile = packed.encode();
            if(!result.catalog.addVariant(variant, report))
            {
                added = false;
                break;
            }
        }
        if(added)
            ++result.describedSlots;
    }

    result.usable = !result.materials.isEmpty() && result.describedSlots > 0;
    if(!result.usable && report)
        report->add("terrain.atlas_analysis", EValidationSeverity::Error,
                    "Automatic atlas analysis found no safe visual materials and transitions");
    else if(result.ambiguousSlots > 0 && report)
        report->add("terrain.atlas_ambiguous", EValidationSeverity::Warning,
                    QString("Excluded %1 ambiguous atlas slot(s); painting near them will fail closed")
                        .arg(result.ambiguousSlots));
    return result;
}
