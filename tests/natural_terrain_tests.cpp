#include <QtCore>
#include <cmath>
#include "natural_terrain.h"
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);int checks=0;
    const auto check=[&](bool ok,const char* text){QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<text<<"\n";if(!ok)throw std::runtime_error(text);++checks;};
    try {
        natural::Grid a;a.width=a.height=257;auto b=a;
        auto parameters=natural::preset("mountains");
        natural::generate(a,"mountains",parameters,220,[](){return false;});natural::generate(b,"mountains",parameters,220,[](){return false;});
        check(a.heights==b.heights,"same seed has exactly the same heights");
        natural::generate(b,"mountains",parameters,221,[](){return false;});check(a.heights!=b.heights,"different seeds change geography");
        check(natural::metrics(a)["slopeP90"].toDouble()>40,"mountain preset has a steep slope population");
        int calls=0;bool stopped=false;try{natural::generate(b,"plain",natural::preset("plain"),220,[&](){return ++calls>4;});}catch(const std::exception&){stopped=true;}
        check(stopped&&calls==5,"cancellation checked at each grid row");
        natural::Grid flat;flat.width=flat.height=65;flat.heights.fill(20,65*65);flat.wetness.fill(0,65*65);
        auto before=flat.heights;natural::erode(flat,{{"thermalIterations",8},{"hydraulicIterations",16}},[](){return false;});check(flat.heights==before,"flat terrain remains flat under erosion");
        auto thermal=flat;thermal.heights[32*65+32]=40;double sum=0;for(double h:thermal.heights)sum+=h;
        natural::erode(thermal,{{"thermalIterations",16},{"hydraulicIterations",0},{"talus",0.65}},[](){return false;});double after=0;for(double h:thermal.heights)after+=h;
        check(std::abs(sum-after)<1e-6&&thermal.heights[32*65+32]<40,"thermal erosion transfers mass and reduces a peak");
        natural::river(flat,{{"points",QJsonArray{QJsonArray{0,32},QJsonArray{64,32}}},{"width",8},{"depth",4},{"level",12}},220,[](){return false;});
        check(flat.sample(30,32)<flat.sample(30,38)&&flat.sample(30,38)<flat.sample(30,55),"river has a low bed, bank and floodplain");
        natural::Grid large;large.width=large.height=513;QElapsedTimer timer;timer.start();natural::generate(large,"hills",natural::preset("hills"),220,[](){return false;});
        check(timer.elapsed()<10000,"16x16 generation below ten seconds on current host");
        QTextStream(stdout)<<"16x16 computeMs="<<timer.elapsed()<<"\nRESULT "<<checks<<" PASS, 0 FAIL\n";return 0;
    }catch(const std::exception& e){qCritical()<<e.what();return 1;}
}
