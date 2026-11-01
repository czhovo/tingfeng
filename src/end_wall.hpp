#pragma once

namespace mj {
struct EndWallRonSources {
    array<double,4> ordinary{}, lastOrdinary{}, extra{};
    array<double,4> withoutLastYaku() const {
        array<double,4> result{};
        for(int p=0;p<4;++p) result[p]=ordinary[p]+extra[p];
        return result;
    }
};
inline EndWallRonSources endWallRonSources(int self,int afterActor,int draws,
                                          const array<bool,4>& locked) {
    EndWallRonSources result;
    if(draws<=0) return result;
    if(afterActor<0 || afterActor>=4) afterActor=self;
    // The tail contains at most three ordinary draws. An extra discard after
    // a call is a separate source and never inherits houtei from remaining==0.
    auto opportunities=normalDrawExposure(self,afterActor,draws);
    double total=0; int canCall=0;
    for(int p=0;p<4;++p) if(p!=self) {
        result.ordinary[p]=opportunities[p]*(locked[p]?LOCKED_RIICHI_SOURCE_MULTIPLIER:1.0);
        total+=result.ordinary[p]; canCall+=!locked[p];
    }
    if(total<=0) return result;
    double callShare=canCall?EXTRA_CALL_SOURCE_SHARE:0;
    for(int p=0;p<4;++p) if(p!=self) {
        result.ordinary[p]*=(1-callShare)/total;
        if(!locked[p]) result.extra[p]=callShare/canCall;
    }
    int last=(afterActor+draws)%4;
    if(last!=self) {
        result.lastOrdinary[last]=result.ordinary[last];
        result.ordinary[last]=0;
    }
    return result;
}
inline void capForecastWin(double& probability,double& gain) {
    if(probability>.65) { gain*=.65/probability; probability=.65; }
}
}
