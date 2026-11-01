#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <limits>
#include <algorithm>
namespace retention {
struct Value {double first=0,second=0,dealIn=0;};
// Continuous event utility, in the same score units as the base payoff.
// No pairwise closeness gate, eligibility band or win-loss veto exists.
inline constexpr double WIN_UNITS=4., DEAL_IN_UNITS=4.;
inline bool nagashiZeroWinCertificate(int shanten,int ownDraws){return ownDraws>=0 && shanten>ownDraws;}
struct Entry {
    double base=0,win=0,dealIn=0;bool known=true,complete=true,riskKnown=true;
    Entry(double b=0,double p=0,bool k=true,bool c=true,double d=0,bool rk=true):base(b),win(p),dealIn(d),known(k),complete(c),riskKnown(rk){}
    bool validWin()const{return known && std::isfinite(win) && win>=-1e-9 && win<=1+1e-9;}
    bool validRisk()const{return riskKnown && std::isfinite(dealIn) && dealIn>=-1e-9 && dealIn<=1+1e-9;}
    bool validBase()const{return complete && std::isfinite(base) && base>-1e19 && (!known || validWin()) &&
        (!riskKnown || validRisk()) && (!known || !riskKnown || win+dealIn<=1+1e-8);}
};
inline double winReward(const Entry& x,double unit){return WIN_UNITS*unit*(x.validWin()?x.win:0.);}
// Unknown probabilities stay unknown. Selection uses an explicitly labelled
// lower utility bound, not a fabricated probability. The adverse unknown
// event can occupy at most the probability not already assigned to own win.
inline double riskPenalty(const Entry& x,double unit){return DEAL_IN_UNITS*unit*(x.validRisk()?x.dealIn:1-(x.validWin()?x.win:0.));}
inline double objective(const Entry& x,double unit){return x.base+winReward(x,unit)-riskPenalty(x,unit);}
template<class Container> inline int select(const Container& entries,double unit){
    int best=-1;
    for(int i=0;i<int(entries.size());++i)if(entries[i].validBase()){
        const double score=objective(entries[i],unit);
        if(best<0 || score>objective(entries[best],unit)+1e-9*unit)best=i;
    }
    return best;
}
inline bool prefer(double b,double p,double r,double proposed,double proposedWin,double proposedRisk,double unit){
    std::array<Entry,2> options{{Entry(b,p,true,true,r),Entry(proposed,proposedWin,true,true,proposedRisk)}};
    return select(options,unit)==1;
}
struct Frontier {
    std::vector<Entry> entries;std::vector<int> labels;
    void addPolicy(double base,double win,double risk,int label=-1){entries.emplace_back(base,win,true,true,risk);labels.push_back(label);}
    int selected(double unit)const{return select(entries,unit);}
};
}
