#pragma once

namespace mj {
inline constexpr double LOCKED_RIICHI_SOURCE_MULTIPLIER=3.0;
inline constexpr double EXTRA_CALL_SOURCE_SHARE=.05;
inline array<double,4> normalDrawExposure(int self,int afterActor,int draws) {
    array<double,4> exposure{};
    if(afterActor<0 || afterActor>=4) afterActor=self;
    for(int step=1;step<=draws;++step) {
        int player=(afterActor+step)%4;
        if(player!=self) exposure[player]+=1;
    }
    return exposure;
}
inline array<double,4> ronSourceWeights(int self,const array<bool,4>& locked,
                                        const array<double,4>& opportunities) {
    array<double,4> weights{};
    double total=0; int canCall=0;
    for(int p=0;p<4;++p) if(p!=self) {
        weights[p]=max(0.0,opportunities[p])*(locked[p]?LOCKED_RIICHI_SOURCE_MULTIPLIER:1.0);
        total+=weights[p]; canCall+=!locked[p];
    }
    if(total<=0) return weights;
    double callShare=canCall?EXTRA_CALL_SOURCE_SHARE:0;
    for(int p=0;p<4;++p) if(p!=self)
        weights[p]=(1-callShare)*weights[p]/total+(!locked[p]?callShare/canCall:0);
    return weights;
}
inline double sourceWeightedRon(const array<double,4>& weights,const array<double,4>& values) {
    double value=0;
    for(int p=0;p<4;++p) if(weights[p]>0) value+=weights[p]*values[p];
    return value;
}
}
