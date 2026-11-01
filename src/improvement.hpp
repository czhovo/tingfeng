#pragma once

namespace mj {
struct ImprovementOffer {
    double probability=0, gain=0, winDelta=0,riskDelta=0;
};
struct ImprovementStep {
    double value=0, winDelta=0,riskDelta=0, scale=1, positiveMass=0, acceptedMass=0, waitedMass=0;
};
inline ImprovementStep chooseImprovement(double waitingValue,double waitingWin,double waitingRisk,
                                         double exitRate,double survival,
                                         const vector<ImprovementOffer>& offers,double baseValue,double baseWin,double baseRisk,double unit) {
    ImprovementStep out;
    double available=max(0.0,1-exitRate);
    for(const auto& offer:offers) if(abs(offer.gain)>1e-12 || abs(offer.winDelta)>1e-12 || abs(offer.riskDelta)>1e-12) out.positiveMass+=offer.probability;
    // Preserve the old per-horizon normalization. Rejecting a weak offer must
    // not inflate the probability of receiving a stronger one.
    if(out.positiveMass>available) out.scale=available/out.positiveMass;
    out.value=available*waitingValue;
    out.winDelta=available*waitingWin;out.riskDelta=available*waitingRisk;
    for(const auto& offer:offers) if(abs(offer.gain)>1e-12 || abs(offer.winDelta)>1e-12 || abs(offer.riskDelta)>1e-12) {
        double q=offer.probability*out.scale;
        if(retention::prefer(baseValue+waitingValue,baseWin+waitingWin,baseRisk+waitingRisk,baseValue+offer.gain,baseWin+offer.winDelta,baseRisk+offer.riskDelta,unit)) {
            out.value+=q*(offer.gain-waitingValue);
            out.winDelta+=q*(offer.winDelta-waitingWin);out.riskDelta+=q*(offer.riskDelta-waitingRisk);
            out.acceptedMass+=q;
        } else out.waitedMass+=q;
    }
    out.value*=survival; out.winDelta*=survival;out.riskDelta*=survival;
    return out;
}
}
