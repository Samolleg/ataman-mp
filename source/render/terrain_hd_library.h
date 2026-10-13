#ifndef TERRAIN_HD_LIBRARY_H
#define TERRAIN_HD_LIBRARY_H
#include <QStringList>
class CTerrainHdProgress;
// Private, portable terrain donors. Import never changes game resources.
namespace TerrainHdLibrary {
QString root();
QStringList configuredSources(const QStringList& archives);
// Read existing local sources only; never fall back to mutable game archives.
QStringList availableSources();
// An existing library is authoritative; game archive changes do not refresh it.
bool ensure(const QStringList& archives, QStringList* localArchives, QString* error,
            CTerrainHdProgress* progress=nullptr, const QString& directory={});
}
#endif
