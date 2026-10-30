#include "../source/resources/terrain_reference_path.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if(!temporary.isValid()) return 1;
    const QString root = temporary.path();
    const QString bundle = root + "/portable/terrain-reference";
    QDir().mkpath(bundle);
    const QString pack = bundle + "/HDPack_Terrain-JamaisVu135-50c285225fb1745e.res";
    const QString explicitPath = root + "/custom.res";
    auto create = [](const QString& path) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write("fixture") == 7; };
    if(!create(pack) || !create(explicitPath)) return 2;
    int checks = 0;
    auto check = [&](bool ok) { ++checks; if(!ok) { std::fprintf(stderr, "FAIL %d\n", checks); return false; } return true; };
    if(!check(terrainReferencePath("", root + "/portable") == pack)) return 3;
    if(!check(terrainReferencePath("  ", root + "/portable") == pack)) return 3;
    if(!check(terrainReferencePath(root + "/old/missing.res", root + "/portable") == pack)) return 3;
    if(!check(terrainReferencePath(explicitPath, root + "/portable") == explicitPath)) return 3;
    QDir().rename(root + "/portable", root + "/moved");
    const QString moved = root + "/moved/terrain-reference/" + QFileInfo(pack).fileName();
    if(!check(terrainReferencePath(pack, root + "/moved") == moved)) return 3;
    if(!check(terrainReferencePath("", root + "/absent").isEmpty())) return 3;
    if(!check(terrainReferencePath(explicitPath, root + "/absent") == explicitPath)) return 3;
    if(!check(terrainReferencePath(root + "/missing.res", root + "/absent") == root + "/missing.res")) return 3;
    std::printf("PASS %d portable reference checks\n", checks);
    return 0;
}
