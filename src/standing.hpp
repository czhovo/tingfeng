#pragma once

// Public match information and an explicit, approximate placement objective.
// No hidden hands, future tiles or opponent identities enter this model.
namespace mj {
struct StandingContext {
    bool enabled=false, last=false, repeat=false, extension=false;
    int remaining=1, handIndex=1, honba=0, sticks=0, returnPoints=25000, endThreshold=30000, bankruptcyBelow=0;
    array<int,4> placementBonuses{{15000,5000,-5000,-15000}};
    array<int,4> scores{{25000,25000,25000,25000}}, tieOrder{{0,1,2,3}}, priority{{0,1,2,3}};
    string source;
    bool referenceValid=false;
    int referenceSelf=-1;
    unsigned referenceReached=0;
    double foldReference=0;
    array<double,4> endReady{};
    map<pair<array<int,4>,int>,double> valueCache;
    unordered_map<uint64_t,double> payoffCache;
    void clearCache() { valueCache.clear(); payoffCache.clear(); referenceValid=false; }
    void load(const json& j) {
        if(j.is_null()) { *this=StandingContext{}; return; }
        if(j.at("schema").get<int>()!=1) throw runtime_error("match context schema must be 1");
        auto sc=j.at("scores"), order=j.at("tie_priority");
        if(!sc.is_array() || sc.size()!=4 || !order.is_array() || order.size()!=4)
            throw runtime_error("match context needs four scores and four tie priorities");
        array<bool,4> used{};
        for(int p=0;p<4;++p) {
            if(!sc[p].is_number_integer() || !order[p].is_number_integer())
                throw runtime_error("match context scores and tie priorities must be integers");
            scores[p]=sc[p].get<int>(); tieOrder[p]=order[p].get<int>();
            if(tieOrder[p]<0 || tieOrder[p]>=4 || used[tieOrder[p]])
                throw runtime_error("invalid match tie priority");
            used[tieOrder[p]]=true; priority[tieOrder[p]]=p;
        }
        remaining=j.at("remaining_scheduled_hands").get<int>();
        handIndex=j.at("hand_index").get<int>(); last=j.at("is_last_scheduled_hand").get<bool>();
        if(remaining<1 || handIndex<1 || last!=(remaining==1)) throw runtime_error("inconsistent remaining hands");
        honba=j.value("honba",0); sticks=j.value("riichi_sticks",0);
        repeat=j.value("dealer_repeat_possible",false); extension=j.value("extension_possible",false);
        returnPoints=j.value("return_points",25000);
        endThreshold=j.value("end_threshold_points",30000);
        // v35's schema-1 prototype used the misleading return_points name
        // for the 30000-point ending threshold. Preserve those saved cases
        // without treating the historical typo as the official return score.
        if(!j.contains("end_threshold_points") && returnPoints==30000) {
            endThreshold=30000; returnPoints=25000;
        }
        placementBonuses={{15000,5000,-5000,-15000}};
        if(j.contains("placement_bonuses")) {
            auto bonus=j.at("placement_bonuses");
            if(!bonus.is_array() || bonus.size()!=4) throw runtime_error("four placement bonuses required");
            for(int p=0;p<4;++p) placementBonuses[p]=bonus[p].get<int>();
        }
        bankruptcyBelow=j.value("bankruptcy_below",0);
        source=j.at("source").get<string>();
        if(honba<0 || sticks<0 || source.empty()) throw runtime_error("invalid public match context");
        enabled=true; clearCache();
    }
    void loadFile() {
        ifstream f("match_context.json");
        if(f) { json j; f>>j; load(j); }
    }
    int rank(const array<int,4>& sc,int who) const {
        int r=0;
        for(int p=0;p<4;++p) if(sc[p]>sc[who] || (sc[p]==sc[who] && priority[p]<priority[who])) ++r;
        return r;
    }
    int after(const array<int,4>& sc,int winner=-1,bool dealerReady=false) const {
        if(*min_element(sc.begin(),sc.end())<bankruptcyBelow) return 0;
        bool dealerContinues=repeat && (winner==0 || (winner<0 && dealerReady));
        if(last && dealerContinues && rank(sc,0)==0 && sc[0]>=endThreshold) return 0;
        int n=max(0,remaining-(dealerContinues?0:1));
        if(n==0 && extension && *max_element(sc.begin(),sc.end())<endThreshold) n=1;
        return n;
    }
    double value(const array<int,4>& sc,int n,int self) {
        // Include self in the cache's integer tag. The public context is fixed
        // during one decision; loading another context invalidates all caches.
        auto key=make_pair(sc,n*4+self);
        auto hit=valueCache.find(key); if(hit!=valueCache.end()) return hit->second;
        // Official supplied four-player rules: 25000 return, no oka,
        // placement bonuses +15/+5/-5/-15 (in thousands of table points).
        // The 30000 ending threshold is a separate rule, not a return value.
        double result=sc[self]-returnPoints;
        if(n<=0) result+=placementBonuses[rank(sc,self)];
        else {
            // Nine-point Gaussian quadrature. Independent future point shocks
            // may be recentered to zero-sum without changing any ranks.
            const double x[9]={-4.512745863,-3.205429003,-2.076847979,-1.023255664,0,
                                1.023255664,2.076847979,3.205429003,4.512745863};
            const double w[9]={.000022345844,.002789141321,.049916406765,.244097502895,.406349206349,
                               .244097502895,.049916406765,.002789141321,.000022345844};
            double sigma=4200*sqrt(double(n)), expected=0;
            for(int k=0;k<9;++k) {
                double own=sc[self]+sigma*x[k]; array<double,4> ranks{}; ranks[0]=1;
                int seen=0;
                for(int p=0;p<4;++p) if(p!=self) {
                    double beat=.5*erfc((own-sc[p])/(sqrt(2.0)*sigma));
                    array<double,4> next{};
                    for(int r=0;r<=seen;++r) { next[r]+=ranks[r]*(1-beat); next[r+1]+=ranks[r]*beat; }
                    ranks=next; ++seen;
                }
                for(int r=0;r<4;++r) expected+=w[k]*ranks[r]*placementBonuses[r];
            }
            result+=expected;
        }
        valueCache.emplace(key,result); return result;
    }
};
inline thread_local StandingContext standing;

struct PositionValue {
    int self; unsigned reached;
    array<int,4> live;
    PositionValue(int who,unsigned mask):self(who),reached(mask),live(standing.scores) {
        for(int p=0;p<4;++p) if(mask&(1u<<p)) live[p]-=1000;
    }
    bool canDeclareRiichi() const { return !standing.enabled || live[self]>=1000; }
    double reference() const {
        if(standing.referenceValid && standing.referenceSelf==self && standing.referenceReached==reached)
            return standing.foldReference;
        return standing.value(live,max(0,standing.remaining-1),self);
    }
    array<int,4> settled(int winner,int from,int points,bool tsumo,bool ownFutureRiichi=false) const {
        auto sc=live;
        bool deposit=ownFutureRiichi && !(reached&(1u<<self));
        if(deposit) sc[self]-=1000;
        int pot=1000*(standing.sticks+__builtin_popcount(reached)+(deposit?1:0));
        if(tsumo) {
            if(winner==0) {
                int each=points/3;
                for(int p=1;p<4;++p) { sc[p]-=each+100*standing.honba; sc[0]+=each+100*standing.honba; }
            } else {
                // A normal child's total tsumo payment uniquely recovers the
                // rounded nondealer share; the dealer pays the remainder.
                int small=((points+399)/400)*100;
                for(int p=0;p<4;++p) if(p!=winner) {
                    int payment=(p==0?points-2*small:small)+100*standing.honba;
                    sc[p]-=payment; sc[winner]+=payment;
                }
            }
        } else {
            int payment=points+300*standing.honba;
            sc[from]-=payment; sc[winner]+=payment;
        }
        sc[winner]+=pot;
        return sc;
    }
    double win(int points,bool tsumo,bool ownFutureRiichi=false,int from=-1) const {
        uint64_t key=(uint64_t(points)<<16)|unsigned(self)|(uint64_t(reached)<<2)|
                     (uint64_t(tsumo)<<6)|(uint64_t(ownFutureRiichi)<<7)|(uint64_t(from+2)<<8);
        auto hit=standing.payoffCache.find(key); if(hit!=standing.payoffCache.end()) return hit->second;
        double before=reference(), result=0;
        if(tsumo || from>=0) {
            auto sc=settled(self,from,points,tsumo,ownFutureRiichi);
            result=standing.value(sc,standing.after(sc,self),self)-before;
        } else {
            for(int p=0;p<4;++p) if(p!=self) {
                auto sc=settled(self,p,points,false,ownFutureRiichi);
                result+=(standing.value(sc,standing.after(sc,self),self)-before)/3;
            }
        }
        standing.payoffCache.emplace(key,result); return result;
    }
    double dealInCost(int winner,int points) const {
        auto sc=settled(winner,self,points,false);
        // Signed relative to the SAME reference as continuation. A cheap
        // payment can occasionally beat a position-losing exhaustive draw.
        return reference()-standing.value(sc,standing.after(sc,winner),self);
    }
    array<int,4> drawScores(unsigned ready,bool ownFutureRiichi=false) const {
        auto sc=live;
        if(ownFutureRiichi && !(reached&(1u<<self))) sc[self]-=1000;
        int n=__builtin_popcount(ready);
        if(n>0 && n<4) for(int p=0;p<4;++p) sc[p]+=(ready&(1u<<p))?3000/n:-3000/(4-n);
        return sc;
    }
    struct DrawSettlement {
        array<int,4> beforeAward{},scores{};
        int futureHands=0,unawardedSticks=0,awardedSticks=0,recipient=-1;
    };
    DrawSettlement settleDraw(unsigned ready,bool ownFutureRiichi=false) const {
        DrawSettlement result;
        result.beforeAward=result.scores=drawScores(ready,ownFutureRiichi);
        // End/extension thresholds are checked BEFORE the unclaimed sticks
        // are collected, as in the supplied official rules.
        result.futureHands=standing.after(result.scores,-1,ready&1u);
        bool deposit=ownFutureRiichi && !(reached&(1u<<self));
        result.unawardedSticks=standing.sticks+__builtin_popcount(reached)+(deposit?1:0);
        if(result.futureHands==0 && result.unawardedSticks>0) {
            for(int p=0;p<4;++p) if(standing.rank(result.scores,p)==0) {
                result.recipient=p;
                result.awardedSticks=result.unawardedSticks;
                result.scores[p]+=1000*result.awardedSticks;
                result.unawardedSticks=0;
                break;
            }
        }
        return result;
    }
    DrawSettlement settleNagashi(unsigned ready,bool ownFutureRiichi=false) const {
        // Nagashi replaces the noten transfer. It is not an ordinary win:
        // no honba, no pot collection, and dealer readiness governs repeat.
        DrawSettlement result;result.scores=live;
        bool deposit=ownFutureRiichi && !(reached&(1u<<self));
        if(deposit)result.scores[self]-=1000;
        for(int payer=0;payer<4;++payer)if(payer!=self) {
            int payment=self==0 || payer==0?4000:2000;
            result.scores[payer]-=payment;result.scores[self]+=payment;
        }
        result.beforeAward=result.scores;
        result.futureHands=standing.after(result.scores,-1,ready&1u);
        result.unawardedSticks=standing.sticks+__builtin_popcount(reached)+(deposit?1:0);
        if(result.futureHands==0 && result.unawardedSticks>0) {
            for(int p=0;p<4;++p)if(standing.rank(result.scores,p)==0) {
                result.recipient=p;result.awardedSticks=result.unawardedSticks;
                result.scores[p]+=1000*result.awardedSticks;result.unawardedSticks=0;break;
            }
        }
        return result;
    }
    double nagashiValue(unsigned ready,bool ownFutureRiichi=false) const {
        auto result=settleNagashi(ready,ownFutureRiichi);
        return standing.value(result.scores,result.futureHands,self);
    }
    double drawValue(unsigned ready,bool ownFutureRiichi=false) const {
        auto result=settleDraw(ready,ownFutureRiichi);
        return standing.value(result.scores,result.futureHands,self);
    }
    double depositCost(unsigned othersReady) const {
        // Existing recurrence uses this as the loss of a NONRETURNED deposit
        // on branches stopped by another winner. The draw reward separately
        // includes any terminal refund; using that refund here as well would
        // wrongly make a leading player's deposit free even when others win.
        auto ordinary=drawScores(othersReady,false),paid=drawScores(othersReady,true);
        return max(0.0,standing.value(ordinary,standing.after(ordinary,-1,othersReady&1u),self)-
                       standing.value(paid,standing.after(paid,-1,othersReady&1u),self));
    }
    json explain() const {
        json values=json::array();
        for(int pts: {1000,2000,3900,8000,12000}) {
            json from=json::object();
            for(int p=0;p<4;++p) if(p!=self) from[to_string(p)]=win(pts,false,false,p);
            values.push_back({{"ron_points",pts},{"average_ron_value",win(pts,false)}, {"payer_values",from}});
        }
        return {{"absolute_scores",live},{"self",self},{"rank",standing.rank(live,self)+1},
                {"remaining_scheduled_hands",standing.remaining},{"can_declare_riichi",canDeclareRiichi()},
                {"return_points",standing.returnPoints},{"end_threshold_points",standing.endThreshold},
                {"placement_bonuses",standing.placementBonuses},
                {"fold_reference",reference()},{"rank_payoffs",values}};
    }
};
}
