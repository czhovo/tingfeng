#pragma once

namespace mj {
inline constexpr double OTHER_TSUMO_RIICHI=.55;
inline constexpr double OTHER_TSUMO_OPEN=.23;
inline constexpr double OTHER_TSUMO_CLOSED=.46;
inline constexpr double CLOSED_TSUMO_MEAN_FACTOR=1.15;

inline int gridTsumoPayment(int index,bool dealer) {
    // Same legal basic-point values as OpponentPayouts' paired ron grid.
    constexpr int base[21]={240,320,400,480,560,640,720,800,880,960,1120,
                           1280,1440,1600,1760,1920,2000,3000,4000,6000,8000};
    int small=(base[index]+99)/100*100,large=(2*base[index]+99)/100*100;
    return dealer?3*large:large+2*small;
}
struct OtherWinInterval {
    double survival=1;
    array<double,2> stopped{}; // normal / newly committed own riichi
};
struct OtherWinModel {
    array<double,4> weight{},actorSurvival{{1,1,1,1}};
    array<array<double,2>,4> actorValue{};
    double fullSurvival=1;
    void add(int drawActor,double mass,double ordinary,double committed) {
        if(mass<=0) return;
        weight[drawActor]+=mass;
        actorValue[drawActor][0]+=mass*ordinary;
        actorValue[drawActor][1]+=mass*committed;
    }
    void finish(double retainedSurvival) {
        double total=accumulate(weight.begin(),weight.end(),0.0);
        if(total<=0) return;
        fullSurvival=clamp(retainedSurvival,1e-12,1.0);
        for(int p=0;p<4;++p) if(weight[p]>0) {
            for(double &value:actorValue[p]) value/=weight[p];
            // Allocate, rather than refit, the existing complete-cycle hazard.
            // The product across the three opponents stays fullSurvival.
            actorSurvival[p]=pow(fullSurvival,weight[p]/total);
        }
    }
    OtherWinInterval interval(int self,int afterActor,int draws,double targetSurvival=-1) const {
        OtherWinInterval out;
        if(afterActor<0 || afterActor>=4) afterActor=self;
        double power=1;
        if(targetSurvival>=0 && fullSurvival<1)
            power=log(clamp(targetSurvival,1e-12,1.0))/log(fullSurvival);
        for(int i=1;i<=draws;++i) {
            int actor=(afterActor+i)%4;
            if(actor==self) continue; // no other tsumo, and ideal fold excludes our ron payment
            double survive=pow(actorSurvival[actor],power);
            for(int branch=0;branch<2;++branch)
                out.stopped[branch]+=out.survival*(1-survive)*actorValue[actor][branch];
            out.survival*=survive;
        }
        return out;
    }
    json explain() const {
        return {{"actor_weights",weight},{"actor_survival",actorSurvival},
                {"actor_values",actorValue},{"full_cycle_survival",fullSurvival}};
    }
};
inline double stoppedContinuation(double ownWinProbability,double ownWinGain,
                                  const OtherWinInterval& interval,double continuation,
                                  bool committed,double riskTax=0) {
    return ownWinGain+(1-ownWinProbability)*
        (interval.survival*continuation+interval.stopped[committed?1:0]-riskTax);
}
inline double scaleAboveFold(double value,double fold,double potential) {
    return fold+potential*(value-fold);
}
}
