#pragma once

namespace mj {
// Opponent waiting events are not independent in reality. Retain the existing
// marginal tile probabilities and use an explicit product approximation for
// the chance that no opponent ends this hand on the proposed action.
inline double surviveImmediateRon(const array<double,4>& probability,int self) {
    double survive=1;
    for(int p=0;p<4;++p) if(p!=self) survive*=1-clamp(probability[p],0.0,1.0);
    return survive;
}
inline double exposedActionValue(double continuation,double survive,double expectedLoss) {
    return survive*continuation-expectedLoss;
}
struct FutureDiscardExposure {
    double survival=1,loss=0;
    double value(double continuation) const { return exposedActionValue(continuation,survival,loss); }
    double win(double probability) const { return survival*probability; }
    double dealIn(double future) const {return 1-survival+survival*future;}
};
}
