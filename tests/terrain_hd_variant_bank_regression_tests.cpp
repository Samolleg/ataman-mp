#include <QtTest>
#include "terrain_hd_variant_bank.h"

class TerrainHdVariantBankRegressionTests:public QObject {
 Q_OBJECT
private slots:
 void waterIsNotOfferedAsUniformVariant(){
  TerrainHdVariantBank::Bank bank;TerrainHdVariantBank::Entry entry;
  entry.material="Ground / Water";entry.reviewedClass=1;entry.physicalClass=1;
  entry.layer="land";entry.id="synthetic-water";bank.entries.append(entry);
  QVERIFY(TerrainHdVariantBank::forMaterial(bank,entry.material,1,ETerrainAtlasLayer::Land).isEmpty());
 }
 void donorMustBeStaticAndUniform(){
  STerrainAtlasProfileFamily family;family.id=17;family.name="Grass / Common";family.tileType=0;
  STerrainAtlasProfileSlot slot;slot.signature.northWest=slot.signature.northEast=slot.signature.southEast=slot.signature.southWest=family.id;
  QVERIFY(TerrainHdVariantBank::uniformDonor(family,slot,0,false));
  QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,0,true));
  for(int type:{6,13,14}){QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,type,false));
   family.tileType=type;QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,0,false));}
  family.tileType=0;slot.signature.southWest=99;QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,0,false));
  slot.signature.southWest=17;slot.assigned=false;QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,0,false));
  slot.assigned=true;slot.ambiguous=true;QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,0,false));
  slot.ambiguous=false;slot.mixedEdgeRecipes=QStringList{"contextual"};QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,0,false));
  slot.mixedEdgeRecipes.clear();slot.routeOwnerMaterial=17;QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,0,false));
  slot.routeOwnerMaterial=0;
  for(const auto& name:QStringList{"Ground / Water","Water / Still","Ground / WATER","Lava / Orange"}){
   family.name=name;QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,1,false));}
  family.name="Ground / Common";family.paintSelectable=false;QVERIFY(!TerrainHdVariantBank::uniformDonor(family,slot,1,false));
 }
};
QTEST_GUILESS_MAIN(TerrainHdVariantBankRegressionTests)
#include "terrain_hd_variant_bank_regression_tests.moc"
