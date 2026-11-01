#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <bits/stdc++.h>
#include <nlohmann/json.hpp>
using namespace std;
using json = nlohmann::json;
#include "retention_policy.hpp"
#include "opponent_constraints.hpp"
#include "win_diagnostics.hpp"
#include "parallel_runtime.hpp"
#include "decision_telemetry.hpp"
#include "parallel_telemetry.hpp"
#include "standing.hpp"
#include "payouts.hpp"
#include "immediate.hpp"
#include "improvement.hpp"
#include "ura.hpp"
#include "ron_sources.hpp"
#include "end_wall.hpp"
#include "other_wins.hpp"
#include "event_hazards.hpp"
#include "declaration_values.hpp"
#include "win_order.hpp"

namespace mj {
using U64 = uint64_t;
thread_local bool debugPolicy=false;
struct Parameters {
    double risk=1,winBase=3500,winSeat=500,drawRank=0.0,pressure=0.02;
    double pon=2.0,chi=1.6,ron=0.3,futureExtra=0.5;
    bool call=true,tail=true;
};
thread_local Parameters cfg;
// v29: use the 5 s action allowance for complete, comparable search passes.
// The remaining margin includes protocol parsing, output, and scheduling.
#ifdef V29_LEGACY_SEARCH
constexpr double SEARCH_SECONDS=.40, IMPROVE_SECONDS=.54, START_IMPROVE_SECONDS=.43;
constexpr int IMPROVE_SHANTEN=1;
#else
#ifndef TINGFENG_SEARCH_SECONDS
#define TINGFENG_SEARCH_SECONDS 3.30
#endif
constexpr double SEARCH_SECONDS=TINGFENG_SEARCH_SECONDS, IMPROVE_SECONDS=SEARCH_SECONDS, START_IMPROVE_SECONDS=SEARCH_SECONDS-.20;
constexpr int IMPROVE_SHANTEN=2;
#endif
thread_local chrono::steady_clock::time_point actionStarted;
struct ActionBudgetExpired {};
thread_local bool enforceActionDeadline=false;
thread_local string deadlineResponse="PASS",deadlineValidActions;
thread_local json deadlineSnapshot=nullptr;
#include "retention_runtime.hpp"
inline bool sharedSearchStopped() {
    if(!prt::request.stop())return false;
    dt::timeLimit(prt::request.timeStop.load());return true;
}
inline void checkActionDeadline(){if(enforceActionDeadline && sharedSearchStopped())throw ActionBudgetExpired{};}
#ifdef PARALLEL_REGRESSION
int parallelTestDepth=9;bool parallelTestNoShape=false;
#endif
int p5[10];
int norm(int t) { return t < 34 ? t : (t - 34) * 9 + 4; }
int tile(const string &s) {
    if (s.size() < 2) return -1;
    int b = s[0]=='W' ? 0 : s[0]=='T' ? 9 : s[0]=='B' ? 18 : 27;
    int n = s[1]-'0';
    if (!n && b<27) return 34+b/9;
    if (n<1 || n>(b==27?7:9)) return -1;
    return b+n-1;
}
string name(int t) {
    if (t<0) return "Z1";
    if (t>=34) return string(1,"WTB"[t-34])+"0";
    return string(1,"WTBZ"[t/9])+char('1'+t%9);
}
bool terminal(int t) { return t>=27 || t%9==0 || t%9==8; }
int doraNext(int t) {
    t=norm(t);
    if (t<27) return t/9*9+(t%9+1)%9;
    if (t<31) return 27+(t-26)%4;
    return 31+(t-30)%3;
}
vector<string> words(const string &s) {
    istringstream in(s); vector<string> v; string w;
    while(in>>w) v.push_back(w);
    return v;
}
vector<string> split(const string &s, char delim) {
    vector<string> v; string z; istringstream in(s);
    while(getline(in,z,delim)) v.push_back(z);
    return v;
}

struct Hand {
    array<int,34> c{};
    array<int,4> code{};
    int n=0;
    void add(int t,int d=1) {
        c[t]+=d; code[t/9]+=p5[t%9]*d; n+=d;
    }
};
struct Pattern { int m,t,p; } pat[50];
vector<U64> suits, honors;
vector<uint32_t> suitCompact, honorCompact;
U64 maskM, maskT, maskP;

void buildTable(vector<U64>& out, int digits, bool honor) {
    int sz=p5[digits]; out.resize(sz); out[0]=1;
    for(int code=1;code<sz;++code) {
        int q=code, i=0;
        while(q%5==0) { q/=5; ++i; }
        int k=q%5, unit=p5[i];
        U64 a=out[code-unit];
        if(k>=3) a|=(out[code-3*unit]&maskM)<<10;
        if(k>=2) {
            U64 b=out[code-2*unit];
            a|=(b&maskT)<<2;
            a|=(b&maskP)<<1;
        }
        if(!honor) {
            if(i<8 && (q/5)%5) {
                a|=(out[code-unit-p5[i+1]]&maskT)<<2;
                if(i<7 && (q/25)%5)
                    a|=(out[code-unit-p5[i+1]-p5[i+2]]&maskM)<<10;
            }
            if(i<7 && (q/25)%5) a|=(out[code-unit-p5[i+2]]&maskT)<<2;
        }
        out[code]=a;
    }
}
uint32_t compact(U64 b) {
    uint32_t out=0; int shift=0;
    for(int p=0;p<2;++p) {
        int best=-1;
        for(int m=4;m>=0;--m) {
            int t=4-m;
            while(t>=0 && !(b&(U64(1)<<(m*10+t*2+p)))) --t;
            if(t>=0 && 2*m+t>best) {
                out|=uint32_t(m*10+t*2+p+1)<<shift;
                shift+=6; best=2*m+t;
            }
        }
    }
    return out;
}
void initTables() {
    p5[0]=1; for(int i=1;i<=9;++i) p5[i]=p5[i-1]*5;
    for(int m=0;m<=4;++m) for(int t=0;t<=4;++t) for(int p=0;p<2;++p) {
        int k=m*10+t*2+p; pat[k]={m,t,p};
        if(m+t<4) { maskM|=U64(1)<<k; maskT|=U64(1)<<k; }
        if(m+t<=4 && p==0) maskP|=U64(1)<<k;
    }
    buildTable(suits,9,false); buildTable(honors,7,true);
    suitCompact.resize(p5[9]); honorCompact.resize(p5[7]);
}
struct ShEntry { U64 key=0; int8_t val=0; };
thread_local vector<ShEntry> shcache(1<<19);
U64 hashHand(const Hand& h,int melds=0) {
    U64 x=U64(h.code[0])*0x9e3779b185ebca87ULL;
    x^=U64(h.code[1])*0xc2b2ae3d27d4eb4fULL;
    x^=U64(h.code[2])*0x165667b19e3779f9ULL;
    x^=U64(h.code[3]+melds*78125)*0x85ebca77c2b2ae63ULL;
    x^=x>>29; x*=0x27d4eb2f165667c5ULL; x^=x>>32;
    return x?x:1ULL;
}
int standardShanten(const Hand &h,int melds) {
    uint32_t opts[4];
    for(int s=0;s<4;++s) {
        uint32_t* slot=&(s==3?honorCompact:suitCompact)[h.code[s]];
        uint32_t v=__atomic_load_n(slot,__ATOMIC_RELAXED);
        if(!v) {
            v=compact((s==3?honors:suits)[h.code[s]]);uint32_t expected=0;
            if(dt::r.active)++dt::r.compact_computations;
            if(!__atomic_compare_exchange_n(slot,&expected,v,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED) && dt::r.active)
                ++dt::r.compact_raced_publications;
        }
        opts[s]=v;
    }
    int best=8;
    for(uint32_t a=opts[0];a;a>>=6) {
        auto x=pat[(a&63)-1];
        for(uint32_t b=opts[1];b;b>>=6) {
            auto y=pat[(b&63)-1]; if(x.p+y.p>1) continue;
            for(uint32_t c=opts[2];c;c>>=6) {
                auto z=pat[(c&63)-1]; if(x.p+y.p+z.p>1) continue;
                int m=melds+x.m+y.m+z.m, t=x.t+y.t+z.t, p=x.p+y.p+z.p;
                for(uint32_t d=opts[3];d;d>>=6) {
                    auto w=pat[(d&63)-1]; if(p+w.p>1 || m+w.m>4) continue;
                    int mm=m+w.m;
                    best=min(best,8-2*mm-min(t+w.t,4-mm)-p-w.p);
                }
            }
        }
    }
    return best;
}
int shanten(const Hand &h,int melds=0) {
    U64 key=hashHand(h,melds); auto &entry=shcache[key&(shcache.size()-1)];
    if(entry.key==key) return entry.val;
    int best=standardShanten(h,melds);
    if(!melds) {
        int pairs=0, kinds=0, orphans=0, opair=0;
        for(int t=0;t<34;++t) {
            kinds+=h.c[t]>0; pairs+=h.c[t]>=2;
            if(terminal(t)) { orphans+=h.c[t]>0; opair|=h.c[t]>=2; }
        }
        best=min(best,6-pairs+max(0,7-kinds));
        best=min(best,13-orphans-opair);
    }
    entry.key=key; entry.val=best; return best;
}

struct Meld {
    vector<int> tiles;
    int kind=0; // 0 sequence, 1 triplet, 2 quad
    bool open=true;
    int called=-1, from=-1;
    int base() const {
        int x=40; for(int t:tiles) x=min(x,norm(t)); return x;
    }
};
struct Discard { int raw, serial; bool drawn; };
struct Player {
    vector<Discard> river;
    vector<Meld> melds;
    bool riichi=false;
    int riichiAt=-1, drawAt=-1, draws=0;
    array<bool,34> safe{};
};
struct State {
    shared_ptr<ohc::Snapshot> opponentConstraints;
    bool nagashiAlive=true;
    // Disabled only for the existing coarse hypothetical-kan calculation,
    // whose extra cycle is a replacement draw, not a normal wall-end event.
    bool allowEndWallForecast=true;
    bool replacementForecast=false;
    int seat=0, round=0, remaining=70, serial=0, consumed=0;
    int actor=-1, drawn=-1, lastDiscard=-1, lastPlayer=-1;
    string event;
    Hand hand;
    array<int,37> raw{};
    array<int,34> publicSeen{}, dora{};
    array<Player,4> pl;
    vector<int> indicators;
    void add(int t,int d) {
        if(t<0) return; raw[t]+=d; hand.add(norm(t),d);
    }
    void updateDora(const json& q) {
        if(!q.contains("doraIndicators") || !q["doraIndicators"].is_string()) return;
        vector<int> next; for(auto s:words(q["doraIndicators"].get<string>())) next.push_back(tile(s));
        indicators=next; dora.fill(0);
        for(int t:indicators) if(t>=0) ++dora[doraNext(t)];
    }
    void discard(int who,int t,bool drawnFlag,bool orderCertain=true) {
        int n=norm(t); ++serial;
        if(who==seat && !terminal(n)) nagashiAlive=false;
        pl[who].river.push_back({t,serial,drawnFlag});
        ++publicSeen[n];
        if(who==seat) add(t,-1);
        for(int p=0;p<4;++p) if(p==who || orderCertain) pl[p].safe[n]=true;
        lastDiscard=t; lastPlayer=who;
    }
    void process(const json& q) {
        updateDora(q);
        if(q.contains("handTiles")) {
            auto w=words(q["handTiles"].get<string>());
            hand=Hand{}; raw.fill(0);
            for(size_t i=1;i<w.size();++i) add(tile(w[i]),1);
            return;
        }
        if(!q.contains("state") || !q["state"].is_string()) return;
        auto w=words(q["state"].get<string>());
        if(w.empty()) return;
        if(w[0]=="0") { seat=stoi(w[1]); round=stoi(w[2]); event="INIT"; return; }
        if(w[0]=="2") {
            actor=seat; event="DRAW"; drawn=tile(w[1]); add(drawn,1);
            --remaining; ++pl[seat].draws; pl[seat].drawAt=serial;
            if(!pl[seat].riichi) {
                pl[seat].safe.fill(false);
                for(auto d:pl[seat].river) pl[seat].safe[norm(d.raw)]=true;
            }
            return;
        }
        if(w[0]!="3" || w.size()<3) return;
        actor=stoi(w[1]); event=w[2]; auto &p=pl[actor];
        if(event=="DRAW") {
            --remaining; ++p.draws; p.drawAt=serial;
            if(!p.riichi) { p.safe.fill(false); for(auto d:p.river) p.safe[norm(d.raw)]=true; }
        } else if(event=="PLAY" || event=="LIZHI") {
            if(event=="LIZHI") { p.riichi=true; p.riichiAt=serial+1; }
            discard(actor,tile(w[3]),q.value("playDrawnTile",false),q.value("historyOrderCertain",true));
        } else if(event=="CHI" || event=="PENG") {
            if(lastPlayer==seat) nagashiAlive=false;
            if(!p.riichi) {
                p.safe.fill(false);
                for(auto d:p.river) p.safe[norm(d.raw)]=true;
            }
            Meld m; m.kind=event=="CHI"?0:1; m.called=lastDiscard; m.from=lastPlayer;
            for(int i=3;i<6;++i) m.tiles.push_back(tile(w[i]));
            bool skipped=false;
            for(int t:m.tiles) {
                if(!skipped && t==lastDiscard) { skipped=true; continue; }
                ++publicSeen[norm(t)]; if(actor==seat) add(t,-1);
            }
            p.melds.push_back(m);
            discard(actor,tile(w[6]),false,q.value("historyOrderCertain",true));
        } else if(event=="GANG") {
            if(lastPlayer==seat) nagashiAlive=false;
            int t=w.size()>3?tile(w[3]):lastDiscard;
            Meld m; m.kind=2; m.called=lastDiscard; m.from=lastPlayer;
            // A red five in an open quad is public even if the notification is normalized.
            int n=norm(t);
            if(n<27 && n%9==4) { m.tiles={n,n,n,34+n/9}; }
            else m.tiles={n,n,n,n};
            bool skipped=false;
            for(int x:m.tiles) {
                if(!skipped && x==lastDiscard) { skipped=true; continue; }
                ++publicSeen[norm(x)]; if(actor==seat) add(x,-1);
            }
            p.melds.push_back(m);
        } else if(event=="ANGANG") {
            int t=tile(w[3]), n=norm(t); Meld m; m.kind=2; m.open=false;
            if(n<27 && n%9==4) m.tiles={n,n,n,34+n/9}; else m.tiles={n,n,n,n};
            for(int x:m.tiles) { ++publicSeen[norm(x)]; if(actor==seat) add(x,-1); }
            p.melds.push_back(m);
        } else if(event=="BUGANG") {
            int t=tile(w[3]); ++publicSeen[norm(t)]; if(actor==seat) add(t,-1);
            for(auto &m:p.melds) if(m.kind==1 && m.base()==norm(t)) { m.kind=2; m.tiles.push_back(t); break; }
        }
    }
    void consume(const json& input) {
        auto &r=input["requests"];
        if((int)r.size()<=consumed) { *this=State{}; }
        for(int i=consumed;i<(int)r.size();++i) process(r[i]);
        consumed=(int)r.size();
    }
    int meldCount() const { return (int)pl[seat].melds.size(); }
    bool closed() const { for(auto &m:pl[seat].melds) if(m.open) return false; return true; }
    int actual(int t) const {
        if(raw[t]) return t;
        if(t<27 && t%9==4 && raw[34+t/9]) return 34+t/9;
        return -1;
    }
};

#include "constraint_binding.hpp"

PositionValue positionOf(const State& st) {
    unsigned mask=0;
    for(int p=0;p<4;++p) if(st.pl[p].riichi) mask|=1u<<p;
    return PositionValue(st.seat,mask);
}
bool canDeclareRiichi(const State& st) {
    return !standing.enabled || positionOf(st).canDeclareRiichi();
}
#include "kokushi_filter.hpp"
#include "kokushi_risk.hpp"

double publicReadyProbability(const State& st,int who) {
    const auto &p=st.pl[who]; int turn=p.river.size(), n=p.melds.size();
    if(p.riichi) return 1;
    if(n==0) {
        double ready=.008+.0015*turn+.00019*turn*turn;
        int streak=0;
        for(auto it=p.river.rbegin();it!=p.river.rend() && it->drawn;++it) ++streak;
        if(turn>=7 && streak>=2) ready+=min(.14,streak*.025);
        return ready;
    }
    const double intercept[]={0,-3.65,-2.45,-1.1,-.3};
    return 1/(1+exp(-(intercept[min(4,n)]+.23*turn)));
}
void prepareStanding(const State& st) {
    standing.clearCache();
    if(!standing.enabled) return;
    auto pos=positionOf(st);
    standing.endReady.fill(0);
    double turns=max(0,st.remaining)/4.0;
    for(int p=0;p<4;++p) if(p!=st.seat) {
        double ready=publicReadyProbability(st,p);
        standing.endReady[p]=st.pl[p].riichi?1:clamp(ready+min(.40,.04*turns)*(1-ready),.01,.96);
    }
    // All offensive and draw values share the same noten-draw reference.
    // Comparing a win to today's uncertain rank but tenpai to a losing draw
    // would use incompatible baselines and overvalue an exhaustive draw.
    double baseline=0;
    for(unsigned mask=0;mask<16;++mask) if(!(mask&(1u<<st.seat))) {
        double probability=1;
        for(int p=0;p<4;++p) if(p!=st.seat)
            probability*=mask&(1u<<p)?standing.endReady[p]:1-standing.endReady[p];
        baseline+=probability*pos.drawValue(mask);
    }
    standing.referenceSelf=st.seat; standing.referenceReached=pos.reached;
    standing.foldReference=baseline; standing.referenceValid=true;
}

struct Context {
    State &st;
    shared_ptr<ohc::Model> constraintModel;
    array<double,34> constraintWallProbability{};
    array<int,34> unseen{}, visible{};
    array<double,34> danger{};
    array<array<double,34>,4> dangerByOpponent{}, ronProbability{};
    array<array<double,34>,4> ronShape{},specialRon{},specialDoubleRon{};
    array<array<array<double,2>,4>,16> specialSingleValue{},specialDoubleValue{};
    array<array<FutureDiscardExposure,2>,TemporalOtherWins::MAX_TIME> futureDrawExposure{};
    array<double,34> immediateSurvival{};
    array<double,4> readyProb{};
    TemporalOtherWins otherWins;
    DeclarationValues declarations;
    array<DeclarationValues::Values,TemporalOtherWins::MAX_TIME> declarationWeights{};
    double threat=0, meanDanger=0, survival=.93;
    int totalUnknown=0, melds=0;
    explicit Context(State &s):st(s),melds(s.meldCount()) {
        visible=s.publicSeen;
        for(int t:s.indicators) if(t>=0) ++visible[norm(t)];
        for(int t=0;t<34;++t) { visible[t]+=s.hand.c[t]; unseen[t]=max(0,4-visible[t]); totalUnknown+=unseen[t]; }
        if(s.opponentConstraints && s.opponentConstraints->used){constraintModel=s.opponentConstraints->model;++constraintModel->metrics.policyContexts;
            const auto& root=*constraintModel->root;if(root.wall>0)for(int t=0;t<34;++t)constraintWallProbability[t]=root.nextWallMean(t,unseen[t])/root.wall;}
    }
    // A hypothetical call changes our locked melds, not the information used
    // to infer opponents. Keep the root public risk and common value reference.
    Context(State& s,const Context& parent):Context(s) {
        unseen=parent.unseen;visible=parent.visible;totalUnknown=parent.totalUnknown;
        constraintModel=parent.constraintModel;constraintWallProbability=parent.constraintWallProbability;
        danger=parent.danger;dangerByOpponent=parent.dangerByOpponent;
        ronProbability=parent.ronProbability;immediateSurvival=parent.immediateSurvival;
        ronShape=parent.ronShape;specialRon=parent.specialRon;specialDoubleRon=parent.specialDoubleRon;
        specialSingleValue=parent.specialSingleValue;specialDoubleValue=parent.specialDoubleValue;
        futureDrawExposure=parent.futureDrawExposure;
        readyProb=parent.readyProb;otherWins=parent.otherWins;
        declarations=parent.declarations;declarationWeights=parent.declarationWeights;
        threat=parent.threat;meanDanger=parent.meanDanger;survival=parent.survival;
    }
    double survivingContinuation(int tile,double value) const {
        return standing.enabled?immediateSurvival[tile]*value:value;
    }
    double actionValue(int tile,double continuation) const {
        // The future value is conditional on this discard passing. It cannot
        // be earned on a branch where an opponent has already won the hand.
        return exposedActionValue(continuation,standing.enabled?immediateSurvival[tile]:1,danger[tile]);
    }
    const DeclarationValues::Values& projectedDeclarations(int elapsed) const {
        return declarationWeights[clamp(elapsed,0,TemporalOtherWins::MAX_TIME-1)];
    }
    void buildFutureExposure();
    void assessThreats() {
        array<PublicHazardPlayer,4> states{};
        if(standing.enabled) {
            auto pos=positionOf(st);unsigned possible=0;
            for(int p=0;p<4;++p) {
                auto& out=states[p];out.locked=st.pl[p].riichi;
                out.turn=st.pl[p].river.size();out.melds=st.pl[p].melds.size();out.ready=publicReadyProbability(st,p);
                for(const auto& meld:st.pl[p].melds)out.open|=meld.open;
                out.canDeclare=p!=st.seat && !out.open && pos.live[p]>=1000;
                if(out.canDeclare)possible|=1u<<p;
            }
            declarations.reset(st.seat,pos.reached,possible);
        }
        double none=1;
        for(int who=0;who<4;++who) if(who!=st.seat) {
            auto &p=st.pl[who]; int turn=p.river.size(), n=p.melds.size();
            double ready=publicReadyProbability(st,who);
            readyProb[who]=ready; none*=1-ready;
            int meldDora=0, yakuhai=0, meldSuit=-1; bool mixed=false, allSimple=true;
            for(auto &m:p.melds) {
                for(int x:m.tiles) {
                    int t=norm(x); meldDora+=st.dora[t]+(x>=34); allSimple&=!terminal(t);
                    if(t<27) { if(meldSuit==-1) meldSuit=t/9; else if(meldSuit!=t/9) mixed=true; }
                }
                if(m.kind && (m.base()>=31 || m.base()==27+who || m.base()==27+st.round)) ++yakuhai;
            }
            double flush=0;
            if(!mixed && meldSuit>=0 && n) {
                int same=0, other=0;
                for(auto d:p.river) if(norm(d.raw)<27) { if(norm(d.raw)/9==meldSuit) ++same; else ++other; }
                flush=clamp(.18*n+.045*other-.20*same-.14,0.0,.9);
                if(n==1 && same>=2) flush*=.4;
            }
            auto publicMeanFor=[&](bool locked) {
                double mean=(locked?5600.0:2300.0)*pow(1.37,min(5,meldDora));
                if(flush>.4)mean=max(mean,3900.0+1800*flush);
                if(who==0)mean*=1.48;
                return min(mean,who==0?18000.0:12000.0);
            };
            double cost=publicMeanFor(p.riichi);
            if(standing.enabled) {
                auto pos=positionOf(st);
                double publicMean=cost;
                bool open=false;
                for(const auto& meld:p.melds) open|=meld.open;
                array<OpponentPayouts,2> ronPayouts{{OpponentPayouts(publicMeanFor(false),who==0),OpponentPayouts(publicMeanFor(true),who==0)}};
                array<OpponentPayouts,2> tsumoPayouts{{OpponentPayouts(publicMeanFor(false)*(open?1:CLOSED_TSUMO_MEAN_FACTOR),who==0),
                                                     OpponentPayouts(publicMeanFor(true)*(open?1:CLOSED_TSUMO_MEAN_FACTOR),who==0)}};
                const auto& payouts=ronPayouts[p.riichi];
                for(unsigned mask:declarations.masks) {
                    bool locked=mask&(1u<<who);
                    auto addOutcome=[&](int payer,int payment,bool tsumo,double mass) {
                        array<double,2> value{};
                        for(int committed=0;committed<2;++committed)
                            value[committed]=declarations.win(mask,who,payer,payment,tsumo,committed!=0);
                        if(tsumo)otherWins.addTsumo(mask,who,mass,value);
                        else otherWins.addRon(mask,who,payer,mass,value);
                    };
                    for(int i=0;i<OpponentPayouts::COUNT;++i) {
                        addOutcome(-1,gridTsumoPayment(i,who==0),true,tsumoPayouts[locked].probability[i]);
                        for(int payer=0;payer<4;++payer) if(payer!=who)
                            addOutcome(payer,ronPayouts[locked].points[i],false,ronPayouts[locked].probability[i]);
                    }
                }
                cost=0;
                json branches=json::array();
                for(int i=0;i<OpponentPayouts::COUNT;++i) {
                    double loss=pos.dealInCost(who,payouts.points[i]);
                    cost+=payouts.probability[i]*loss;
                    if(debugPolicy) {
                        auto sc=pos.settled(who,st.seat,payouts.points[i],false);
                        branches.push_back({{"points",payouts.points[i]},{"probability",payouts.probability[i]},
                                            {"loss_value",loss},{"rank_after",standing.rank(sc,st.seat)+1}});
                    }
                }
                if(debugPolicy) cerr<<json({{"opponent_payout",who},{"riichi",p.riichi},
                    {"ready_probability",ready},{"public_mean",publicMean},{"distribution_mean",payouts.mean()},
                    {"expected_loss_value",cost},{"branches",branches}}).dump()<<'\n';
            } else cost+=5000.0-850.0*st.seat;
            cost*=cfg.risk;
            for(int t=0;t<34;++t) {
                if(p.safe[t]) continue;
                double risk=0;
                int u=unseen[t];
                if(t>=27) {
                    const double honorRisk[5]={0,.009,.028,.060,.081};
                    risk=honorRisk[u];if(constraintModel)risk*=constraintModel->countRatio(who,t,1,2);
                    if(t>=31 || t==27+who || t==27+st.round) risk*=1.12;
                    if(n && !yakuhai && allSimple && flush<.3) risk*=.62;
                } else {
                    int r=t%9;
                    auto available=[&](int a,int b) {
                        if(!unseen[a] || !unseen[b]) return 0.0;
                        double value=min(1.15,sqrt(double(unseen[a]*unseen[b]))/3.1);
                        return constraintModel?value*constraintModel->neighborRatio(who,a,b):value;
                    };
                    if(r<=6) {
                        if(r==6) risk+=.016*available(t+1,t+2);
                        else if(!p.safe[t+3]) risk+=.043*available(t+1,t+2);
                    }
                    if(r>=2) {
                        if(r==2) risk+=.016*available(t-1,t-2);
                        else if(!p.safe[t-3]) risk+=.043*available(t-1,t-2);
                    }
                    if(r>=1 && r<=7) risk+=.018*available(t-1,t+1);
                    if(u>=1) risk+=.0065*sqrt(double(u)/3)*(constraintModel?constraintModel->countRatio(who,t,1,1):1.);
                    if(u>=2) risk+=.014*(u-1)/2*(constraintModel?constraintModel->countRatio(who,t,2,2):1.);
                    if(n && !yakuhai && allSimple && (r==0 || r==8)) risk*=.7;
                    if(n>=3) risk*=.88;
                }
                if(flush) {
                    if(t>=27 || t/9==meldSuit) risk*=1+.65*flush;
                    else risk*=1-.85*flush;
                }
                if(st.dora[t]) risk*=1.1;
                if(p.riichi && p.riichiAt<20 && t>=27) risk*=1.1;
                ronShape[who][t]=min(.27,risk);
                ronProbability[who][t]=min(.27,risk)*ready;
                dangerByOpponent[who][t]=min(.27,risk)*ready*cost;
                danger[t]+=dangerByOpponent[who][t];
            }
        }
        // Separate a physically possible kokushi-ready hypothesis from the
        // existing ordinary-hand model. Only public history and our own known
        // inventory enter the inference. Its pursuit probability alone never
        // suppresses ordinary danger: only the joint ready-hand mass is used.
        if(standing.enabled)for(int who=0;who<4;++who)if(who!=st.seat) {
            const auto& player=st.pl[who];
            if(player.riichi || !player.melds.empty() || player.river.empty())continue;
            auto special=infer::predict(unseen,player,constraintModel.get(),who);auto pos=positionOf(st);
            int single=who==0?48000:32000,twice=2*single;
            double singleLoss=pos.dealInCost(who,single),doubleLoss=pos.dealInCost(who,twice);
            if(any_of(special.ron.begin(),special.ron.end(),[](double p){return p>0;}))
                for(unsigned mask:declarations.masks)for(int committed=0;committed<2;++committed) {
                    specialSingleValue[mask][who][committed]=declarations.win(mask,who,st.seat,single,false,committed!=0);
                    specialDoubleValue[mask][who][committed]=declarations.win(mask,who,st.seat,twice,false,committed!=0);
                }
            for(int t=0;t<34;++t) {
                danger[t]-=dangerByOpponent[who][t];
                double specialLoss=(special.ron[t]-special.doubleRon[t])*singleLoss+special.doubleRon[t]*doubleLoss;
                dangerByOpponent[who][t]=(1-special.ready)*dangerByOpponent[who][t]+cfg.risk*specialLoss;
                ronProbability[who][t]=(1-special.ready)*ronProbability[who][t]+special.ron[t];
                ronShape[who][t]*=1-special.ready;
                specialRon[who][t]=special.ron[t];specialDoubleRon[who][t]=special.doubleRon[t];
                danger[t]+=dangerByOpponent[who][t];
            }
            if(debugPolicy)cerr<<json({{"kokushi_model",who},{"joint_ready",special.ready},{"pursuit",special.pursuit},
                {"inference_skipped_structural_impossibility",special.structurallyImpossible},
                {"seed_ready",special.seedReady},{"ron_probability",special.ron},{"double_ron_probability",special.doubleRon},
                {"single_payment",single},{"single_loss_value",singleLoss},{"double_loss_value",doubleLoss}}).dump()<<'\n';
        }
        for(int t=0;t<34;++t) {
            array<double,4> probability{};
            for(int p=0;p<4;++p) probability[p]=ronProbability[p][t];
            immediateSurvival[t]=surviveImmediateRon(probability,st.seat);
        }
        threat=1-none;
        // The generic future-discard allowance remains a nonnegative cost;
        // immediate outcomes above retain their exact signed settlement value.
        for(int t=0;t<34;++t) meanDanger+=constraintModel?max(0.0,danger[t])*constraintWallProbability[t]:max(0.0,danger[t])*unseen[t]/max(1,totalUnknown);
        double opp=0;
        for(int p=0;p<4;++p) if(p!=st.seat) opp+=readyProb[p];
        survival=clamp(.941-.105*opp,.63,.945);
        if(standing.enabled) {
            otherWins.configure(st.seat,st.actor,st.remaining,st.replacementForecast,states);
            for(int elapsed=0;elapsed<TemporalOtherWins::MAX_TIME;++elapsed)
                declarationWeights[elapsed]=declarations.weights(otherWins.project(elapsed));
            survival=otherWins.fullSurvival;
            buildFutureExposure();
            if(debugPolicy) cerr<<json({{"other_wins",otherWins.explain()},
                {"reference",positionOf(st).reference()}}).dump()<<'\n';
        }
    }
};

#include "future_discard_model.hpp"

struct Group { int base, kind; bool open; };
struct WinScore {
    double ron=0, tsumo=0, ronUtility=-1e30, tsumoUtility=-1e30;
    array<double,4> ronByPayer{{-1e30,-1e30,-1e30,-1e30}};
    // Future declaration masks: ron by payer (0..3), then tsumo (4).
    // The raw/current-state fields above remain valid scoring diagnostics.
    array<DeclarationValues::Values,5> futureUtility{};
    int ronHan=0, tsumoHan=0, ronFu=0, tsumoFu=0; bool yaku=false;
};

int points(int han,int fu,bool dealer,bool tsumo,int yakuman=0) {
    if(han<=0 && !yakuman) return 0;
    int base;
    if(yakuman) base=8000*yakuman;
    else if(han>=13) base=8000;
    else if(han>=11) base=6000;
    else if(han>=8) base=4000;
    else if(han>=6) base=3000;
    else if(han>=5) base=2000;
    else base=min(2000,fu*(1<<(han+2)));
    auto up=[](int x) { return (x+99)/100*100; };
    if(!tsumo) return up(base*(dealer?6:4));
    if(dealer) return up(base*2)*3;
    return up(base*2)+2*up(base);
}

struct Scorer {
    const Hand &hand;
    State &st;
    int win, discardRaw, riichi;
    bool closed, dealer, expected=true, lastNormalTile=false;
    int bonus=0, suitMask=0, honorsCount=0, total=0;
    bool allSimple=true, allOutside=true, allHonor=true, allTerminal=true, allGreen=true;
    int flushHan=0, commonYakuman=0;
    array<int,34> full{};
    vector<Group> groups;
    WinScore result;
    const ExpectedBonuses* expectedBonuses=nullptr;
    array<int,ExpectedBonuses::MAX_EXTRA+1> bestRon{},bestTsumo{};
    Scorer(const Hand& h,State &s,int w,int r,int out,const ExpectedBonuses* expectation=nullptr,bool lastNormal=false):hand(h),st(s),win(w),discardRaw(out),riichi(r),closed(s.closed()),dealer(s.seat==0),lastNormalTile(lastNormal) {
        expectedBonuses=expectation;
        full=h.c;
        for(auto &m:st.pl[st.seat].melds) {
            groups.push_back({m.base(),m.kind,m.open});
            for(int t:m.tiles) { ++full[norm(t)]; if(t>=34) ++bonus; }
        }
        for(int suit=0;suit<3;++suit) if(st.raw[34+suit] && h.c[suit*9+4] && discardRaw!=34+suit) ++bonus;
        for(int t=0;t<34;++t) if(full[t]) {
            total+=full[t]; bonus+=st.dora[t]*full[t];
            allSimple &= !terminal(t); allOutside &= terminal(t);
            allHonor &= t>=27; allTerminal &= t<27 && terminal(t);
            allGreen &= (t==10 || t==11 || t==12 || t==14 || t==16 || t==32);
            if(t<27) suitMask|=1<<(t/9); else honorsCount+=full[t];
        }
        if(suitMask && !(suitMask&(suitMask-1))) flushHan=honorsCount?(closed?3:2):(closed?6:5);
        commonYakuman=int(allHonor)+int(allTerminal)+int(allGreen);
        if(closed && groups.empty() && !honorsCount && suitMask && !(suitMask&(suitMask-1))) {
            int b=(suitMask==1?0:suitMask==2?9:18); bool chuuren=full[b]>=3 && full[b+8]>=3;
            for(int i=1;i<8;++i) chuuren &= full[b+i]>=1;
            if(chuuren) {
                auto before=full;--before[win];bool pure=before[b]==3 && before[b+8]==3;
                for(int i=1;i<8;++i)pure &= before[b+i]==1;
                commonYakuman+=pure?2:1;
            }
        }
    }
    void accept(int han,int fu,bool tsumo,int yakuman) {
        // Haitei/houtei are real yaku, unlike dora: apply before the yaku gate.
        // This branch describes a final NORMAL draw/discard only. Yakuman
        // payments ignore additional ordinary yaku.
        if(lastNormalTile && !yakuman) ++han;
        if(han<=0 && !yakuman) return;
        result.yaku=true;
        double value=0;
        if(riichi && expected) {
            if(!expectedBonuses) throw runtime_error("riichi expectation needs a public ura inventory");
            auto &best=tsumo?bestTsumo:bestRon;
            for(int extra=0;extra<=ExpectedBonuses::MAX_EXTRA;++extra) {
                double p=expectedBonuses->extra[extra]; if(p<=0) continue;
                int payment=points(han+bonus+extra,fu,dealer,tsumo,yakuman);
                best[extra]=max(best[extra],payment);
                value+=p*payment;
            }
        } else {
            value=points(han+bonus,fu,dealer,tsumo,yakuman);

        }
        // Nominal han/fu describe the best single expected decomposition for
        // diagnostics. Final expected payments use the legal best decomposition
        // separately for EACH realized extra-han outcome in finishExpected().
        if(tsumo) {
            if(value>result.tsumo) {
                result.tsumo=value; result.tsumoHan=han+bonus; result.tsumoFu=fu;
                if(standing.enabled && !(riichi && expected))
                    result.tsumoUtility=positionOf(st).win(int(value),true,riichi!=0);
            }
        } else if(value>result.ron) {
            result.ron=value; result.ronHan=han+bonus; result.ronFu=fu;
            if(standing.enabled && !(riichi && expected)) {
                auto pos=positionOf(st); result.ronUtility=0;
                for(int p=0;p<4;++p) if(p!=st.seat) {
                    result.ronByPayer[p]=pos.win(int(value),false,riichi!=0,p);
                    result.ronUtility+=result.ronByPayer[p]/3;
                }
            }
        }
    }
    void finishExpected() {
        if(!riichi || !expected || !result.yaku) return;
        result.ron=result.tsumo=0;
        bool ron=any_of(bestRon.begin(),bestRon.end(),[](int p){return p>0;});
        bool tsumo=any_of(bestTsumo.begin(),bestTsumo.end(),[](int p){return p>0;});
        if(standing.enabled) {
            if(ron) { result.ronUtility=0; result.ronByPayer.fill(0); }
            if(tsumo) result.tsumoUtility=0;
        }
        auto pos=positionOf(st);
        for(int extra=0;extra<=ExpectedBonuses::MAX_EXTRA;++extra) {
            double p=expectedBonuses->extra[extra]; if(p<=0) continue;
            result.ron+=p*bestRon[extra]; result.tsumo+=p*bestTsumo[extra];
            if(standing.enabled) {
                if(bestRon[extra]>0) for(int payer=0;payer<4;++payer) if(payer!=st.seat) {
                    double utility=pos.win(bestRon[extra],false,true,payer);
                    result.ronByPayer[payer]+=p*utility;
                    result.ronUtility+=p*utility/3;
                }
                if(bestTsumo[extra]>0) result.tsumoUtility+=p*pos.win(bestTsumo[extra],true,true);
            }
        }
    }
    void special() {
        if(!groups.empty()) return;
        int pairs=0, types=0, op=0;
        for(int t=0;t<34;++t) {
            pairs+=hand.c[t]==2;
            if(terminal(t)) { types+=hand.c[t]>0; op|=hand.c[t]>=2; }
        }
        if(types==13 && op) {
            auto before=hand.c;--before[win];bool thirteen=true;
            for(int t=0;t<34;++t)if(terminal(t))thirteen &= before[t]==1;
            int units=commonYakuman+(thirteen?2:1);
            accept(0,0,false,units);accept(0,0,true,units);return;
        }
        if(pairs==7) {
            int han=2+riichi+allSimple+flushHan+(allOutside?2:0);
            accept(han,25,false,commonYakuman);
            accept(han+1,25,true,commonYakuman);
        }
    }
    void scoreGroups(int pair,int role,bool tsumo) {
        int sequences=0,trips=0,concealedTrips=0,quads=0,dragons=0,winds=0;
        int seq[3][7]={},tri[3][9]={};
        int han=riichi+(closed && tsumo?1:0)+(allSimple?1:0)+flushHan;
        int fu=20+(closed && !tsumo?10:0)+(tsumo?2:0);
        int yakuman=commonYakuman;
        bool outside=terminal(pair), containsHonor=pair>=27;
        bool ryanmen=false;
        int waitFu=0;
        if(role==-1) waitFu=2;
        else if(groups[role].kind==0) {
            int b=groups[role].base;
            if(win==b+1 || (b%9==0 && win==b+2) || (b%9==6 && win==b)) waitFu=2;
            else ryanmen=true;
        }
        for(int i=0;i<(int)groups.size();++i) {
            auto g=groups[i]; int t=g.base;
            if(g.kind==0) {
                ++sequences; ++seq[t/9][t%9];
                outside &= t%9==0 || t%9==6;
            } else {
                ++trips; quads+=g.kind==2;
                bool isOpen=g.open || (!tsumo && role==i);
                concealedTrips+=!isOpen;
                outside &= terminal(t); containsHonor |= t>=27;
                int f=isOpen?2:4;
                if(terminal(t)) f*=2;
                if(g.kind==2) f*=4;
                fu+=f;
                if(t>=31) { ++han; ++dragons; }
                if(t>=27 && t<31) { ++winds; if(t==27+st.seat) ++han; if(t==27+st.round) ++han; }
                if(t<27) ++tri[t/9][t%9];
            }
        }
        int pairFu=0;
        if(pair>=31) pairFu+=2;
        if(pair==27+st.seat) pairFu+=2;
        if(pair==27+st.round) pairFu+=2;
        bool pinfu=closed && sequences==4 && pairFu==0 && ryanmen;
        if(pinfu) ++han;
        if(trips==4) han+=2;
        if(concealedTrips>=3) han+=2;
        if(concealedTrips==4 && (tsumo || role==-1)) yakuman+=role==-1?2:1;
        if(quads==3) han+=2;
        if(quads==4)yakuman+=1;
        if(dragons==2 && pair>=31) han+=2;
        if(dragons==3) yakuman+=1;
        if(winds==4)yakuman+=2;else if(winds==3 && pair>=27 && pair<31)yakuman+=1;
        if(allOutside) han+=2;
        if(outside && sequences) han+=containsHonor?(closed?2:1):(closed?3:2);
        int twins=0;
        for(int s=0;s<3;++s) {
            if(seq[s][0] && seq[s][3] && seq[s][6]) han+=closed?2:1;
            for(int i=0;i<7;++i) twins+=seq[s][i]/2;
        }
        if(closed && twins) han+=twins>=2?3:1;
        for(int i=0;i<7;++i) if(seq[0][i] && seq[1][i] && seq[2][i]) han+=closed?2:1;
        for(int i=0;i<9;++i) if(tri[0][i] && tri[1][i] && tri[2][i]) han+=2;
        fu+=pairFu+waitFu;
        if(pinfu && tsumo) fu=20;
        else { if(!closed && fu==20) fu=30; fu=(fu+9)/10*10; }
        accept(han,fu,tsumo,yakuman);
    }
    void decomposition(array<int,34>& c,int pos,int pair,int fixed) {
        while(pos<34 && !c[pos]) ++pos;
        if(pos==34) {
            if(groups.size()!=4) return;
            if(pair==win) { scoreGroups(pair,-1,false); scoreGroups(pair,-1,true); }
            for(int i=fixed;i<4;++i) {
                auto g=groups[i];
                if((g.kind && win==g.base) || (!g.kind && win>=g.base && win<=g.base+2)) {
                    scoreGroups(pair,i,false); scoreGroups(pair,i,true);
                }
            }
            return;
        }
        if(groups.size()>=4) return;
        if(c[pos]>=3) {
            c[pos]-=3; groups.push_back({pos,1,false}); decomposition(c,pos,pair,fixed); groups.pop_back(); c[pos]+=3;
        }
        if(pos<27 && pos%9<7 && c[pos+1] && c[pos+2]) {
            --c[pos]; --c[pos+1]; --c[pos+2]; groups.push_back({pos,0,false});
            decomposition(c,pos,pair,fixed); groups.pop_back(); ++c[pos]; ++c[pos+1]; ++c[pos+2];
        }
    }
    WinScore run() {
        for(int t=0;t<34;++t) if(full[t]>4) return result;
        special(); int fixed=(int)groups.size();
        array<int,34> c=hand.c;
        for(int t=0;t<34;++t) if(c[t]>=2) {
            c[t]-=2; decomposition(c,0,t,fixed); c[t]+=2;
        }
        finishExpected();
        return result;
    }
};

WinScore scoreWin(const Hand& h,State &st,int win,int riichi,int out=-1,const ExpectedBonuses* expectation=nullptr,bool lastNormalTile=false) {
    return Scorer(h,st,win,riichi,out,expectation,lastNormalTile).run();
}

int ukeire(Hand &h,int m,const array<int,34>& unseen,int old=-2) {
    if(old==-2) old=shanten(h,m);
    int u=0;
    for(int t=0;t<34;++t) if(h.c[t]<4 && unseen[t]>0) {
        h.add(t); if(shanten(h,m)<old) u+=unseen[t]; h.add(t,-1);
    }
    return u;
}
double shapeValue(const Hand& h,Context& cx) {
    double v=0;
    for(int t=0;t<34;++t) if(h.c[t]) {
        double useful=1.0;
        if(h.c[t]==1) {
            useful=0.15;
            if(t<27) {
                if(t%9>0 && h.c[t-1]) useful+=0.5;
                if(t%9<8 && h.c[t+1]) useful+=0.5;
                if(t%9>1 && h.c[t-2]) useful+=0.2;
                if(t%9<7 && h.c[t+2]) useful+=0.2;
            }
        }
        v+=cx.st.dora[t]*h.c[t]*min(1.0,useful)*1.6;
        if(t>=27) {
            bool value=t==27+cx.st.seat || t==27+cx.st.round || t>=31;
            if(h.c[t]>=2 && value) v+=1.8;
            if(h.c[t]==1) v+=value ? 0.18*cx.unseen[t] : 0.03*cx.unseen[t];
        } else if(h.c[t]==1) {
            int r=t%9;
            v+=(r>=2 && r<=6 ? .14 : (r==1 || r==7 ? .08 : 0));
        }
    }
    return v;
}

constexpr int HORIZON=19;
double openPotential(const Hand& h,Context &cx);
struct Future {
    array<double,HORIZON> v{}, win{}, dealIn{};
    int uke=0;
    bool riichi=false;
};

#include "call_shapes.hpp"

struct Evaluator {
    bool budgetRoot=true;
    void enterNode(){if(budgetRoot)prt::request.charge();++nodes;}
#ifdef DT_COUNTER_ORACLE
    ~Evaluator(){dt::oracle(nodes,shapeNodes);}
#endif
    Context &cx;
    int horizon, tailDraws=0, distance=4, rootOut=-1, timeOffset=0;
    uint64_t nodes=0;
    bool exhausted=false, abortable=false;
    uint64_t shapeNodes=0;
    double pool, winBonus, readyReward, riichiReadyReward, riichiDeposit=1000, notenDrawValue=0;
    double meanDrawSurvival=1;
    array<bool,34> furiten{};
    // Hypothetical draws stay visible after being discarded. Equal final
    // hands can have different remaining copies and permanent furiten.
    Hand futureDraws;
#include "constraint_search.hpp"
    U64 futureDiscards=0;
    array<double,4> fullRonSources{},tailAfterSelf{},tailAfterActor{};
    array<double,HORIZON> foldFull{},foldRoot{};
    unordered_map<U64,Future> cache, improvementCache;
    unordered_map<unsigned,OrderedWinInterval> orderedIntervals;
    // A context lives for one decision and its melds, indicators and raw reds
    // are immutable. Reuse structural waits and scores across path histories;
    // depletion, furiten and future value still use the complete path key.
    unordered_map<U64,U64> structuralWaits;
    unordered_map<BonusScoreKey,WinScore,BonusScoreHash> handScores;
    unordered_map<uint64_t,ExpectedBonuses> bonusDistributions;
    chrono::steady_clock::time_point started=actionStarted;
    Evaluator(Context &c,int offset=0,const Evaluator* terminalFrom=nullptr):cx(c),timeOffset(offset) {
        dt::SearchScope telemetryScope;dt::evaluator();budgetRoot=terminalFrom==nullptr;checkActionDeadline();
        distance=(cx.st.seat-cx.st.actor+4)%4;
        if(!distance) distance=4;
        horizon=clamp(cx.st.remaining>=distance?1+(cx.st.remaining-distance)/4:0,0,HORIZON-1);
        tailDraws=cx.st.remaining>=distance?(cx.st.remaining-distance)%4:cx.st.remaining;
        if(cx.st.replacementForecast) {
            // The immediate replacement draw uses one remaining tile. It is
            // followed by ordinary turns; do not invent three opponent draws.
            horizon=clamp(1+max(0,cx.st.remaining-1)/4,0,HORIZON-1);
            tailDraws=max(0,cx.st.remaining-1)%4;
            distance=0;
        }
        if(!cfg.tail)tailDraws=0;
        array<bool,4> locked{}; array<double,4> fullOpportunities{};
        for(int p=0;p<4;++p) { locked[p]=cx.st.pl[p].riichi; if(p!=cx.st.seat) fullOpportunities[p]=1; }
        fullRonSources=ronSourceWeights(cx.st.seat,locked,fullOpportunities);
        tailAfterSelf=ronSourceWeights(cx.st.seat,locked,normalDrawExposure(cx.st.seat,cx.st.seat,tailDraws));
        tailAfterActor=ronSourceWeights(cx.st.seat,locked,normalDrawExposure(cx.st.seat,cx.st.actor,tailDraws));
        if(debugPolicy && standing.enabled) cerr<<json({{"ron_sources",true},{"horizon",horizon},{"tail_draws",tailDraws},
            {"self",cx.st.seat},{"current_actor",cx.st.actor},{"full",fullRonSources},
            {"tail_after_self",tailAfterSelf},{"tail_after_actor",tailAfterActor}}).dump()<<'\n';
        pool=max(18.0,cx.totalUnknown-min(13.0,1.1*horizon));
        if(standing.enabled && cx.totalUnknown>0) {
            double survived=0;
            for(int t=0;t<34;++t)survived+=cx.unseen[t]*cx.immediateSurvival[t];
            meanDrawSurvival=clamp(survived/cx.totalUnknown,0.,1.);
            if(cx.constraintModel){meanDrawSurvival=0;for(int t=0;t<34;++t)meanDrawSurvival+=cx.constraintWallProbability[t]*cx.immediateSurvival[t];}
        }
        winBonus=standing.enabled?0:cfg.winBase+cfg.winSeat*cx.st.seat;
        if(terminalFrom) {
            // A call changes the live-tile assignment and our melds, but its
            // end clock and original public settlement scenario are identical.
            // Reuse only this terminal payoff; all four draw clocks still get
            // distinct waits, fold paths, source order and elapsed lookups.
            if(!standing.enabled || cx.st.seat!=terminalFrom->cx.st.seat ||
               cx.st.remaining+timeOffset!=terminalFrom->cx.st.remaining+terminalFrom->timeOffset ||
               cx.declarations.known!=terminalFrom->cx.declarations.known ||
               cx.declarations.possible!=terminalFrom->cx.declarations.possible ||
               cx.declarations.reference!=terminalFrom->cx.declarations.reference)
                throw runtime_error("future-call terminal scenario mismatch");
            readyReward=terminalFrom->readyReward;
            riichiReadyReward=terminalFrom->riichiReadyReward;
            riichiDeposit=terminalFrom->riichiDeposit;
            notenDrawValue=terminalFrom->notenDrawValue;
        } else {
        readyReward=2200+150*cx.st.seat;
        riichiReadyReward=readyReward-1000;
        if(standing.enabled || cfg.drawRank>=0) {
            readyReward=riichiReadyReward=0;
            if(standing.enabled) riichiDeposit=0;
            array<double,4> endReady{};
            for(int p=0;p<4;++p) if(p!=cx.st.seat)
                endReady[p]=cx.st.pl[p].riichi?1:clamp(cx.readyProb[p]+min(.40,.04*horizon)*(1-cx.readyProb[p]),.01,.96);
            if(standing.enabled) endReady=standing.endReady;
            for(int mask=0;mask<16;++mask) if(!(mask&(1<<cx.st.seat))) {
                double prob=1;
                for(int p=0;p<4;++p) if(p!=cx.st.seat)prob*=mask&(1<<p)?endReady[p]:1-endReady[p];
                if(prob==0)continue;
                if(standing.enabled) {
                    auto pos=positionOf(cx.st);
                    unsigned withSelf=unsigned(mask)|(1u<<cx.st.seat);
                    double base=pos.drawValue(mask,false);
                    readyReward+=prob*(pos.drawValue(withSelf,false)-base);
                    riichiReadyReward+=prob*(pos.drawValue(withSelf,true)-base);
                    // All three terminal types now settle the deposit directly:
                    // our win, an exhaustive draw, and another player's win.
                    continue;
                }
                auto result=[&](bool ready,bool riichi) {
                    int bits=mask|(ready?(1<<cx.st.seat):0),n=__builtin_popcount((unsigned)bits);
                    array<int,4> scores{};
                    for(int p=0;p<4;++p) {
                        scores[p]=25000-1000*(cx.st.pl[p].riichi || (p==cx.st.seat && riichi));
                        if(n>0 && n<4)scores[p]+=(bits&(1<<p))?3000/n:-3000/(4-n);
                    }
                    int rank=1;
                    for(int p=0;p<4;++p)if(scores[p]>scores[cx.st.seat] || (scores[p]==scores[cx.st.seat] && p<cx.st.seat))++rank;
                    return pair<int,int>(scores[cx.st.seat],rank);
                };
                auto b=result(false,false),d=result(true,false),r=result(true,true);
                readyReward+=prob*(d.first-b.first+cfg.drawRank*(b.second-d.second));
                riichiReadyReward+=prob*(r.first-b.first+cfg.drawRank*(b.second-r.second));
            }
        }
        if(standing.enabled) {
            auto end=cx.otherWins.project(cx.st.remaining+timeOffset);
            notenDrawValue=cx.declarations.draw(false,false,end);
            readyReward=cx.declarations.draw(true,false,end);
            riichiReadyReward=cx.declarations.draw(true,true,end);
        }
        }
        if(debugPolicy && standing.enabled) cerr<<json({{"draw_value_breakdown",true},
            {"horizon",horizon},{"tail_draws",tailDraws},{"ready_reward",readyReward},
            {"riichi_ready_reward",riichiReadyReward},{"riichi_deposit_cost",riichiDeposit},
            {"opponents_reference_ready",standing.endReady},{"noten_draw_value",notenDrawValue},
            {"terminal_projection",cx.otherWins.project(cx.st.remaining+timeOffset).explain()},
            {"fold_reference",positionOf(cx.st).reference()}}).dump()<<'\n';
        for(auto d:cx.st.pl[cx.st.seat].river) furiten[norm(d.raw)]=true;
        // A future-call child evaluates exactly one depth-zero concealed
        // shape. Root searches retain their original capacities.
        cache.reserve(terminalFrom?4:8192);
        structuralWaits.reserve(terminalFrom?4:2048);
        handScores.reserve(terminalFrom?32:4096);
        if(standing.enabled) {
            auto tail=otherInterval(cx.st.seat,tailDraws,-1,max(0,cx.st.remaining-tailDraws));
            foldFull[0]=tail.stopped[0]+tail.survival*notenDrawValue;
            auto rootTail=tailInterval(false);
            foldRoot[0]=horizon==0?rootTail.stopped[0]+rootTail.survival*notenDrawValue:foldFull[0];
            for(int k=1;k<HORIZON;++k) {
                auto full=otherInterval(cx.st.seat,4,-1,max(0,cx.st.remaining-(4*k+tailDraws)));
                foldFull[k]=full.stopped[0]+full.survival*foldFull[k-1];
                auto interval=cycleInterval(k,false);
                foldRoot[k]=interval.stopped[0]+interval.survival*foldRoot[k-1];
            }
            if(debugPolicy) cerr<<json({{"fold_continuation",foldRoot[horizon]},
                {"horizon",horizon},{"tail_draws",tailDraws},{"distance_to_own_draw",distance}}).dump()<<'\n';
        }
    }
    OtherWinInterval otherInterval(int after,int draws,double target=-1,int elapsed=0) const {
        return cx.otherWins.interval(cx.st.seat,after,draws,target,elapsed+timeOffset);
    }
    OrderedWinInterval buildWinInterval(int after,int opponentDraws,int elapsed,bool committed,bool ownDraw,int ownElapsed);
    OrderedWinInterval winInterval(int after,int opponentDraws,int elapsed,bool committed,bool ownDraw,int ownElapsed);
    Future readyOrdered(Hand& h,bool riichi);
    WinScore estimatedWinScore(const Hand& h);
    vector<CallContinuation> callContinuations(const Hand& h,int sh,int depth);
    retention::Value advanceCalls(const vector<CallContinuation>& calls,int after,int draws,
        int remaining,int elapsed,double continuation,double win,double risk=0) const;
    double pressuredSurvival(bool riichi) const {
        // Calibrated quiet positions can already exceed .98. A declaration
        // must not make the old pressure cap increase opponent hazard.
        return riichi?max(cx.survival,min(.98,cx.survival+cfg.pressure*(1-cx.threat))):cx.survival;
    }
    bool deferredRiichi(bool riichi,int k) const {
        return standing.enabled && riichi && !cx.st.pl[cx.st.seat].riichi &&
            futureDraws.n==0 && k==horizon && (cx.st.actor!=cx.st.seat || cx.st.replacementForecast);
    }
    OtherWinInterval tailInterval(bool riichi) const {
        if(!standing.enabled) { OtherWinInterval out;out.survival=pow(cx.survival,tailDraws/4.0);return out; }
        int after=horizon==0 && futureDraws.n==0?cx.st.actor:cx.st.seat;
        int elapsed=horizon==0 && futureDraws.n==0?0:max(0,cx.st.remaining-tailDraws);
        return otherInterval(after,tailDraws,pressuredSurvival(riichi),elapsed);
    }
    OtherWinInterval cycleInterval(int k,bool riichi) const {
        if(!standing.enabled) { OtherWinInterval out;out.survival=pressuredSurvival(riichi);return out; }
        bool root=futureDraws.n==0 && k==horizon;
        int elapsed=root?0:max(0,cx.st.remaining-(4*k+tailDraws));
        return otherInterval(root?cx.st.actor:cx.st.seat,root?distance:4,
                                     pressuredSurvival(riichi && !deferredRiichi(riichi,k)),elapsed);
    }
    int cycleElapsed(int k) const {
        return futureDraws.n==0 && k==horizon?0:max(0,cx.st.remaining-(4*k+tailDraws));
    }
    int cycleDistance(int k) const { return futureDraws.n==0 && k==horizon?distance:4; }
    int ownWinTime(int k) const { return cycleElapsed(k)+max(0,cycleDistance(k)-1); }
    double futureTsumoValue(const WinScore& score,int elapsed) const {
        return cx.declarations.average(score.futureUtility[4],cx.projectedDeclarations(elapsed+timeOffset));
    }
    double futureRonValue(const WinScore& score,const array<double,4>& sources,int after,int draws,int elapsed) const {
        double value=0;
        for(int payer=0;payer<4;++payer)if(sources[payer]>0) {
            int distance=(payer-after+4)%4;if(!distance)distance=4;
            // Normal discards use the state BEFORE that player's draw; a
            // declaration on the winning discard is not accepted. The small
            // pooled call-source component has no known exact call time.
            int time=elapsed+(distance<=draws?distance-1:max(0,draws/2));
            value+=sources[payer]*cx.declarations.average(score.futureUtility[payer],cx.projectedDeclarations(time+timeOffset));
        }
        return value;
    }
    double foldFloor(int k) const { return standing.enabled?(futureDraws.n?foldFull[k]:foldRoot[k]):0; }
    double withPotential(double value,double potential) const {
        return scaleAboveFold(value,foldFloor(horizon),potential);
    }
    bool canPlanRiichi() const {
        if(!cx.st.closed() || !canDeclareRiichi(cx.st) || cx.st.remaining<4) return false;
        if(rootOut>=0 && futureDraws.n==0 && timeOffset==0 && cx.st.event=="DRAW" && cx.st.actor==cx.st.seat && !cx.st.replacementForecast){
            bool allowed=false;for(auto part:split(deadlineValidActions,',')){auto w=words(part);if(w.empty()||w[0]!="LIZHI")continue;
                if(w.size()==1)allowed=true;for(size_t i=1;i<w.size();++i)if(tile(w[i])==rootOut)allowed=true;}
            if(!allowed)return false;
        }
        if(cx.st.replacementForecast && futureDraws.n==0) return cx.st.remaining-1>=4;
        // A PASS cannot announce riichi before our next draw, and at that
        // future discard the ordinary four-tiles-left requirement still holds.
        if(standing.enabled && futureDraws.n==0 && cx.st.actor!=cx.st.seat)
            return cx.st.remaining-distance>=4;
        return true;
    }
    int available(const Hand &h,int t) {
#ifdef V30_LEGACY_PATH
        return max(0,cx.unseen[t]-max(0,h.c[t]-cx.st.hand.c[t]));
#else
        return max(0,cx.unseen[t]-futureDraws.c[t]);
#endif
    }
    bool discardedWait(const Hand& h,int t) const {
        if(furiten[t] || (rootOut>=0 && norm(rootOut)==t)) return true;
#ifdef V30_LEGACY_PATH
        return cx.st.hand.c[t]>h.c[t];
#else
        return (futureDiscards & (U64(1)<<t))!=0;
#endif
    }
    U64 searchKey(const Hand& h,int depth) const {
        U64 key=hashHand(h,cx.melds)^(U64(depth+1)*0x3c79ac492ba7b653ULL)^(U64(rootOut+2)*0x1c69b3f74ac4ae35ULL);
#ifndef V30_LEGACY_PATH
        if(futureDraws.n) key^=hashHand(futureDraws)*0xd6e8feb86659fd93ULL;
        if(cx.constraintModel && futureWallDraws.n)key^=hashHand(futureWallDraws)*0x8a5cd789635d2dffULL;
        key^=futureDiscards*0xa0761d6478bd642fULL;
#endif
        return key;
    }
    double openRate(const Hand& h,int t,int added=0) {
        if(cx.st.closed()) return 1;
        if(!cfg.call)return 1.13;
        Hand before=h;if(added)before.add(t,-added);
        int sh=shanten(before,cx.melds);bool pon=false,chi=false;
        for(const auto& option:futureCallShapes(before,t,cx.melds)) {
            if(option.pon)pon=true;else chi=true;
        }
        // The generic fallback has no concrete child hand. Its call count
        // still requires a legal improving discard after locking the meld.
        return pon?cfg.pon:chi?cfg.chi:1;
    }
    const array<double,4>& tailRonSources() const {
        // With no future own draw, a PASS decision starts after the current
        // actor. Descendants following a hypothetical own draw start after self.
        return horizon==0 && futureDraws.n==0?tailAfterActor:tailAfterSelf;
    }
    double ronUtilityForSources(int points,bool riichi,int elapsed=0) {
        array<double,4> values{};
        for(int p=0;p<4;++p) if(p!=cx.st.seat)
            values[p]=cx.declarations.average(cx.declarations.ownWin(points,false,riichi,p),cx.projectedDeclarations(elapsed+timeOffset));
        return sourceWeightedRon(fullRonSources,values);
    }
    double utility(double pointValue) {
        if(standing.enabled) return ronUtilityForSources(int(lround(pointValue)),false);
        int sticks=0;
        for(int p=0;p<4;++p) if(p!=cx.st.seat && cx.st.pl[p].riichi) ++sticks;
        return pointValue+winBonus+1000*sticks;
    }
    double nagashiValue(const Hand& h,int discarded=-1) {
        dt::SearchScope telemetryScope;
        auto &st=cx.st;
        if((!standing.enabled && (st.seat!=0 || !st.pl[st.seat].melds.empty())) || !st.nagashiAlive || st.pl[st.seat].river.size()<5 || st.remaining>40) return standing.enabled?-1e30:0;
        if(discarded>=0 && !terminal(norm(discarded))) return standing.enabled?-1e30:0;
        int reserve=0,unseenOutside=0;
        for(int t=0;t<34;++t) if(terminal(t)) { reserve+=h.c[t]; unseenOutside+=cx.unseen[t]; }
        double q=clamp(double(unseenOutside)/max(1,cx.totalUnknown),0.0,1.0);
        if(cx.constraintModel){q=0;const auto& view=currentConstraintView();for(int t=0;t<34;++t)if(terminal(t))q+=view.wall[t];}
        array<double,20> dp{},next{}; dp[min(19,reserve)]=1;
        for(int k=0;k<horizon;++k) {
            next.fill(0);
            for(int r=0;r<20;++r) {
                next[r]+=q*dp[r];
                if(r) next[r-1]+=(1-q)*dp[r];
            }
            dp=next;
        }
        double keep=accumulate(dp.begin(),dp.end(),0.0);
        if(discarded>=0) {
            int t=norm(discarded); double call=0;
            for(int p=0;p<4;++p) if(p!=st.seat && !st.pl[p].riichi) {
                double share=double(13-3*st.pl[p].melds.size())/max(1,cx.totalUnknown);
                int u=cx.unseen[t];
                double willingness=t>=31 || t==27+p || t==27+st.round?.75:.12;
                call+=u*(u-1)*.5*share*share*willingness;
                if(t<27 && p==(st.seat+1)%4) {
                    int a=t%9==0?t+1:t-1,b=t%9==0?t+2:t-2;
                    call+=cx.unseen[a]*cx.unseen[b]*share*share*.32;
                }
            }
            keep*=1-min(.65,call);
        }
        double survive=pow(cx.survival,st.remaining/4.0)*pow(.955,horizon);
        double reward=(st.seat==0?12000:8000)+winBonus;
        if(standing.enabled) {
            Hand current=h;bool selfReady=waitMask(current)!=0;
            reward=cx.declarations.nagashi(selfReady,cx.otherWins.project(st.remaining));
            auto other=cx.otherWins.interval(st.seat,st.actor,st.remaining);
            double success=keep*pow(.955,horizon);
            return other.stopped[0]+other.survival*(success*reward+(1-success)*notenDrawValue);
        }
        return keep*survive*reward;
    }
    double estimatedHan(const Hand& h) {
        double bonus=0;
        for(int t=0;t<34;++t) if(h.c[t]) {
            double keep=1;
            if(h.c[t]==1) {
                keep=.30;
                if(t<27) {
                    if(t%9>0 && h.c[t-1]) keep+=.4;
                    if(t%9<8 && h.c[t+1]) keep+=.4;
                    if(t%9>1 && h.c[t-2]) keep+=.2;
                    if(t%9<7 && h.c[t+2]) keep+=.2;
                }
            }
            bonus+=cx.st.dora[t]*h.c[t]*min(1.0,keep);
        }
        for(int suit=0;suit<3;++suit) if(cx.st.raw[34+suit] && h.c[suit*9+4] && rootOut!=34+suit) bonus+=.9;
        double yaku=cx.st.closed()?2.2:1.25;
        if(standing.enabled && cx.st.closed() && !canDeclareRiichi(cx.st)) yaku-=1;
        for(auto &m:cx.st.pl[cx.st.seat].melds) for(int t:m.tiles) bonus+=cx.st.dora[norm(t)]+(t>=34);
        if(cx.st.closed()) {
            for(int t=27;t<34;++t) if(h.c[t]>=2 && (t>=31 || t==27+cx.st.seat || t==27+cx.st.round)) yaku+=h.c[t]>=3?.8:.25;
        }
        return clamp(yaku+bonus,1.0,13.0);
    }
    double estimatePoints(const Hand& h,bool asUtility=false,int elapsed=0) {
        double han=estimatedHan(h);
        int lo=int(han),hi=min(13,lo+1);
        if(asUtility && standing.enabled) {
            bool riichi=cx.st.closed() && canDeclareRiichi(cx.st);
            auto reward=[&](int h) {
                double ron=ronUtilityForSources(points(h,35,cx.st.seat==0,false),riichi,elapsed);
                double tsumo=cx.declarations.average(cx.declarations.ownWin(points(h,35,cx.st.seat==0,true),true,riichi),cx.projectedDeclarations(elapsed+timeOffset));
                return (.65*ron+tsumo)/1.65;
            };
            return (1-han+lo)*reward(lo)+(han-lo)*reward(hi);
        }
        return (1-han+lo)*points(lo,35,cx.st.seat==0,false)+(han-lo)*points(hi,35,cx.st.seat==0,false);
    }
    U64 waitMask(Hand& h) {
        U64 key=hashHand(h,cx.melds);
#ifndef V31_NO_SHARED_CACHE
        auto hit=structuralWaits.find(key);
        if(hit!=structuralWaits.end()) return hit->second;
#endif
        U64 mask=0;
        auto own=h.c;
        for(auto &m:cx.st.pl[cx.st.seat].melds) for(int t:m.tiles) ++own[norm(t)];
        for(int t=0;t<34;++t) if(own[t]<4) {
            h.add(t); bool win=shanten(h,cx.melds)==-1; h.add(t,-1);
            if(win) mask|=U64(1)<<t;
        }
#ifndef V31_NO_SHARED_CACHE
        structuralWaits.emplace(key,mask);
#endif
        return mask;
    }
    int uraIndicatorCount() const {
        int quads=0;
        for(const auto &p:cx.st.pl) for(const auto &m:p.melds) quads+=m.kind==2;
        // A hypothetical kan has a new paired indicator even before its face
        // is observed. Unobserved future kans beyond this state are not modeled.
        return max(int(cx.st.indicators.size()),1+quads);
    }
    array<int,5> uraInventory(const Hand& complete,int win) const {
        array<int,34> full=complete.c;
        for(const auto& meld:cx.st.pl[cx.st.seat].melds)
            for(int t:meld.tiles) ++full[norm(t)];
        array<int,5> bins{};
        for(int indicator=0;indicator<34;++indicator) {
            int value=full[doraNext(indicator)];
            if(value<0 || value>4) throw runtime_error("invalid own hand for ura expectation");
            // cx.unseen excludes current public tiles, visible indicators and
            // our ENTIRE real hand (including the root discard). All simulated
            // draws stay known after discard. ready() adds the final winning
            // tile only to 'complete', so remove that one extra copy here.
            int available=cx.unseen[indicator]-futureDraws.c[indicator]-(indicator==win);
            if(available<0) throw runtime_error("overdrawn ura indicator inventory");
            bins[value]+=available;
        }
        return bins;
    }
    WinScore cachedWinScore(const Hand& h,int win,bool riichi,bool lastNormalTile=false) {
        int redOut=rootOut>=34?rootOut-33:0;
        U64 shape=hashHand(h,cx.melds)^(U64(win+1)*0x94d049bb133111ebULL)^
                  (U64(riichi)*0xbf58476d1ce4e5b9ULL)^(U64(redOut)*0x8cb92baa3f3d8dd7ULL);
        array<int,5> inventory{};
        uint64_t inventoryKey=0;
        if(riichi) {
            inventory=uraInventory(h,win);
            inventoryKey=ExpectedBonuses::inventoryKey(inventory,uraIndicatorCount());
        }
        BonusScoreKey key{shape,inventoryKey,lastNormalTile};
#ifndef V31_NO_SHARED_CACHE
        auto hit=handScores.find(key);
        if(hit!=handScores.end()) return hit->second;
#endif
        const ExpectedBonuses* expectation=nullptr;
        if(riichi) {
            auto bonus=bonusDistributions.find(inventoryKey);
            if(bonus==bonusDistributions.end())
                bonus=bonusDistributions.emplace(inventoryKey,ExpectedBonuses(inventory,uraIndicatorCount())).first;
            expectation=&bonus->second;
        }
        if(dt::r.active)++dt::r.hand_score_computations;
        Scorer scorer(h,cx.st,win,riichi?1:0,rootOut,expectation,lastNormalTile);
        WinScore result=scorer.run();
        if(standing.enabled) {
            auto add=[&](int payer,int payment,double probability) {
                if(payment<=0 || probability<=0)return;
                bool tsumo=payer==4;
                const auto& utility=cx.declarations.ownWin(payment,tsumo,riichi,tsumo?-1:payer);
                for(unsigned mask:cx.declarations.masks)result.futureUtility[payer][mask]+=probability*utility[mask];
            };
            if(riichi) {
                for(int extra=0;extra<=ExpectedBonuses::MAX_EXTRA;++extra) {
                    double probability=expectation->extra[extra];if(probability<=0)continue;
                    add(4,scorer.bestTsumo[extra],probability);
                    for(int payer=0;payer<4;++payer)if(payer!=cx.st.seat)add(payer,scorer.bestRon[extra],probability);
                }
            } else {
                add(4,int(lround(result.tsumo)),1);
                for(int payer=0;payer<4;++payer)if(payer!=cx.st.seat)add(payer,int(lround(result.ron)),1);
            }
        }
#ifndef V31_NO_SHARED_CACHE
        handScores.emplace(key,result);
#endif
        return result;
    }
    Future ready(Hand& h,bool riichi) {
        if(standing.enabled)return readyOrdered(h,riichi);
        Future f; U64 waits=waitMask(h); bool furi=false;
        for(U64 left=waits;left;left&=left-1)
            if(discardedWait(h,__builtin_ctzll(left))) furi=true;
        bool endWall=cfg.tail && cx.st.allowEndWallForecast && cx.st.remaining>0;
        bool lastSelf=endWall && tailDraws==0 && horizon>0;
        array<bool,4> locked{};
        for(int p=0;p<4;++p) locked[p]=cx.st.pl[p].riichi;
        int after=horizon==0 && futureDraws.n==0?cx.st.actor:cx.st.seat;
        auto sources=endWallRonSources(cx.st.seat,after,tailDraws,locked);
        auto ordinaryTail=sources.withoutLastYaku();
        bool rootPartial=standing.enabled && futureDraws.n==0 && horizon>0 && (cx.st.actor!=cx.st.seat || cx.st.replacementForecast);
        int prefixDiscards=max(0,distance-1);
        auto prefixSources=ronSourceWeights(cx.st.seat,locked,normalDrawExposure(cx.st.seat,cx.st.actor,prefixDiscards));
        double probability=0,gain=0,lastProbability=0,lastGain=0,tailProbability=0,tailGain=0;
        double prefixProbability=0,prefixGain=0;
        array<double,HORIZON> projectedGain{};
        int tailElapsed=horizon==0 && futureDraws.n==0?0:max(0,cx.st.remaining-tailDraws);
        for(U64 left=waits;left;left&=left-1) {
            int t=__builtin_ctzll(left);
            int u=available(h,t); if(!u) continue;
            f.uke+=u;
            h.add(t);
            WinScore sc=cachedWinScore(h,t,riichi);
            WinScore end=endWall?cachedWinScore(h,t,riichi,true):sc;
            WinScore early=sc,firstTsumo=lastSelf && horizon==1?end:sc;
            bool deferred=deferredRiichi(riichi,horizon);
            if(rootPartial && deferred) {
                early=cachedWinScore(h,t,false);
                firstTsumo=lastSelf && horizon==1?cachedWinScore(h,t,false,true):early;
            }
            h.add(t,-1);
            double self=u/pool,tsumoSelf=self,ronSelf=self;array<double,4> release{};
            if(cx.constraintModel){tsumoSelf=conditionedDrawRate(t);release=conditionedReleaseRatios(t);double factor=0;for(int p=0;p<4;++p)factor+=fullRonSources[p]*release[p];ronSelf*=factor;}
            auto ronFactor=[&](bool declared) {
                double factor=t>=27?(declared?1.35:1.70):terminal(t)?(declared?1.0:1.25):(declared?.70:.85);
                if(!cx.st.closed()) factor+=cfg.ron*(terminal(t)? .75:1.0)*(1-cx.threat);
                if(cx.st.dora[t]) factor*=.8;
                return furi?0:factor;
            };
            double rfactor=ronFactor(riichi);
            if(standing.enabled)for(int k=1;k<HORIZON;++k) {
                bool prefix=rootPartial && k==horizon;
                const auto& tsumo=prefix?firstTsumo:(lastSelf && k==1?end:sc);
                const auto& ron=prefix?early:sc;
                if(tsumo.tsumo>0)projectedGain[k]+=self*futureTsumoValue(tsumo,ownWinTime(k));
                if(ron.ron>0) {
                    double p=self*(prefix?ronFactor(riichi && !deferred)*prefixDiscards/3.0:rfactor);
                    projectedGain[k]+=p*futureRonValue(ron,prefix?prefixSources:fullRonSources,
                        prefix?cx.st.actor:cx.st.seat,prefix?prefixDiscards:3,cycleElapsed(k));
                }
            }
            if(rootPartial) {
                if(firstTsumo.tsumo>0) {
                    prefixProbability+=self;
                    prefixGain+=self*firstTsumo.tsumoUtility;
                }
                if(early.ron>0) {
                    double p=self*ronFactor(riichi && !deferred)*prefixDiscards/3.0;
                    prefixProbability+=p;
                    prefixGain+=p*sourceWeightedRon(prefixSources,early.ronByPayer);
                }
            }
            if(sc.tsumo>0) { probability+=tsumoSelf; gain+=tsumoSelf*(standing.enabled?sc.tsumoUtility:utility(sc.tsumo)); }
            if(end.tsumo>0) { lastProbability+=tsumoSelf; lastGain+=tsumoSelf*(standing.enabled?end.tsumoUtility:utility(end.tsumo)); }
            if(sc.ron>0) {
                double value=standing.enabled?sourceWeightedRon(fullRonSources,sc.ronByPayer):utility(sc.ron);
                probability+=ronSelf*rfactor;gain+=ronSelf*rfactor*value;
                // In the last full cycle, opponents discard BEFORE our final
                // normal draw. Those ron opportunities receive no houtei.
                lastProbability+=ronSelf*rfactor;lastGain+=ronSelf*rfactor*value;
                double mass=accumulate(ordinaryTail.begin(),ordinaryTail.end(),0.0);
                if(cx.constraintModel){mass=0;for(int p=0;p<4;++p)mass+=ordinaryTail[p]*release[p];}
                tailProbability+=self*rfactor*mass;
                tailGain+=self*rfactor*(standing.enabled?futureRonValue(sc,ordinaryTail,after,tailDraws,tailElapsed):mass*utility(sc.ron));
            }
            if(end.ron>0) {
                double mass=accumulate(sources.lastOrdinary.begin(),sources.lastOrdinary.end(),0.0);
                if(cx.constraintModel){mass=0;for(int p=0;p<4;++p)mass+=sources.lastOrdinary[p]*release[p];}
                tailProbability+=self*rfactor*mass;
                tailGain+=self*rfactor*(standing.enabled?futureRonValue(end,sources.lastOrdinary,after,tailDraws,tailElapsed):mass*utility(end.ron));
            }
        }
        if(standing.enabled)for(int k=1;k<HORIZON;++k) {
            double p=rootPartial && k==horizon?prefixProbability:(lastSelf && k==1?lastProbability:probability);
            if(p>.65)projectedGain[k]*=.65/p;
        }
        capForecastWin(probability,gain);
        capForecastWin(lastProbability,lastGain);
        capForecastWin(prefixProbability,prefixGain);
        double fraction=tailDraws/3.0;
        tailProbability*=fraction; tailGain*=fraction;
        capForecastWin(tailProbability,tailGain);
        f.win[0]=tailProbability;
        double endReward=riichi?riichiReadyReward+riichiDeposit:readyReward;
        auto tail=tailInterval(riichi);
        f.v[0]=stoppedContinuation(tailProbability,tailGain,tail,waits?endReward:notenDrawValue,riichi);
        double tax=cx.meanDanger*(riichi?1.00:.28);
        for(int t=1;t<HORIZON;++t) {
            double p=lastSelf && t==1?lastProbability:probability;
            double g=lastSelf && t==1?lastGain:gain;
            if(rootPartial && t==horizon) { p=prefixProbability;g=prefixGain; }
            if(standing.enabled)g=projectedGain[t];
            auto interval=cycleInterval(t,riichi);
            double continuation=f.v[t-1],continuingWin=f.win[t-1],futureTax=tax;
            // A current wait can win before our next discard (the live policy
            // accepts HU). The option to dismantle it is available AFTER that
            // future draw, not as a fictitious immediate zero-win fold.
            if(!riichi && retention::prefer(continuation-futureTax,continuingWin,0,foldFloor(t-1),0,0,retentionUnit())) {
                continuation=foldFloor(t-1);continuingWin=0;futureTax=0;
            }
            f.win[t]=p+(1-p)*interval.survival*continuingWin;
            f.v[t]=stoppedContinuation(p,g,interval,continuation,riichi && !deferredRiichi(riichi,t),futureTax);
        }
        if(riichi) {
            double deposit=riichiDeposit;
            for(int t=0;t<HORIZON;++t) f.v[t]-=deposit*(1-f.win[t]);
        }
        f.riichi=riichi;
        return f;
    }
    Future generic(Hand& h,int sh) {
        Future f; sh=clamp(sh,1,8);
        double weighted=0;
        for(int t=0;t<34;++t) if(available(h,t)>0 && h.c[t]<4) {
            h.add(t); bool better=shanten(h,cx.melds)<sh; h.add(t,-1);
            if(better) {
                int u=available(h,t);f.uke+=u;weighted+=cx.constraintModel?pool*conditionedGenericRate(h,t,u):u*openRate(h,t);

            }
        }
        double reward=standing.enabled?estimatePoints(h,true):utility(estimatePoints(h));
        array<array<double,HORIZON>,9> value{},wins{},risks{};
        value[0][0]=readyReward-(cx.st.closed()?850:0);
        if(cfg.drawRank>=0 && cx.st.closed())value[0][0]=riichiReadyReward;
        if(standing.enabled) value[0][0]=cx.st.closed() && canDeclareRiichi(cx.st)?
            max(readyReward,riichiReadyReward):readyReward;
        if(standing.enabled) {
            auto tail=tailInterval(false);
            value[0][0]=tail.stopped[0]+tail.survival*value[0][0];
            for(int s=1;s<=sh;++s) value[s][0]=foldFloor(0);
        }
        // Tail calls require actual source, locked shape and paid discard.
        // They are expanded by complete search layers, not a free noten bonus.
        const double effective[9]={0,17,27,35,43,48,53,58,60};
        double qwin=min(.5,5.6*1.65/pool);
        WinScore approximate;
        if(standing.enabled)approximate=estimatedWinScore(h);
        for(int t=1;t<HORIZON;++t) {
            auto interval=cycleInterval(t,false);
            wins[0][t]=qwin+(1-qwin)*interval.survival*wins[0][t-1];
            risks[0][t]=(1-qwin)*interval.survival*risks[0][t-1];
            double currentReward=standing.enabled?estimatePoints(h,true,ownWinTime(t)):reward;
            value[0][t]=stoppedContinuation(qwin,qwin*currentReward,interval,value[0][t-1],false);
            if(standing.enabled) {
                bool root=futureDraws.n==0 && t==horizon;
                int prior=max(0,cycleDistance(t)-1);
                auto ordered=winInterval(root?cx.st.actor:cx.st.seat,prior,cycleElapsed(t),false,true,ownWinTime(t));
                ordered.addTsumo(qwin/1.65,approximate);
                ordered.addRon(qwin*.65/1.65*prior/3.,approximate,approximate,false);
                ordered.cap();
                auto result=ordered.settle(value[0][t-1],wins[0][t-1],0,1,risks[0][t-1]);
                value[0][t]=result.first;wins[0][t]=result.second;risks[0][t]=result.dealIn;
            }
            for(int s=1;s<=sh;++s) {
                double q=(s==sh?weighted:effective[s]*(cx.st.closed()?1:cfg.call?1+cfg.futureExtra/s:1.13))/pool;
                q=min(.75,q);
                double nextValue=(1-q)*value[s][t-1]+q*value[s-1][t-1];
                double nextWin=(1-q)*wins[s][t-1]+q*wins[s-1][t-1];
                double nextRisk=(1-q)*risks[s][t-1]+q*risks[s-1][t-1];
                if(standing.enabled) {
                    auto exposure=averageExposure(.16,ownWinTime(t));
                    value[s][t]=interval.stopped[0]+interval.survival*exposure.value(nextValue);
                    wins[s][t]=interval.survival*exposure.win(nextWin);
                    risks[s][t]=interval.survival*exposure.dealIn(nextRisk);
                } else {
                    value[s][t]=interval.stopped[0]+interval.survival*nextValue-cx.meanDanger*.16;
                    wins[s][t]=interval.survival*nextWin;risks[s][t]=interval.survival*nextRisk;
                }
                if(retention::prefer(value[s][t],wins[s][t],risks[s][t],foldFloor(t),0,0,retentionUnit())) { value[s][t]=foldFloor(t);wins[s][t]=0;risks[s][t]=0; }
            }
        }
        f.v=value[sh]; f.win=wins[sh];f.dealIn=risks[sh]; return f;
    }
    bool budgetExpired() {
        if(abortable && (exhausted || sharedSearchStopped() || 
            dt::timeLimit(chrono::duration<double>(chrono::steady_clock::now()-started).count()>SEARCH_SECONDS))) {
            exhausted=true;return true;
        }
        return false;
    }
    Future eval(Hand& h,int sh,int depth) {
        dt::SearchScope telemetryScope;dt::evalCall();
        if(!abortable)checkActionDeadline();
        if(budgetExpired())return Future{};
        depth=min(depth,sh);
        U64 key=searchKey(h,depth);
        auto it=cache.find(key); if(it!=cache.end()) return it->second;
        enterNode();dt::node();

        Future f;
        if(sh==0) {
            f=ready(h,false);
            if(canPlanRiichi()) {
                Future r=ready(h,true);
                for(int t=0;t<HORIZON;++t) if(t>=1 && retention::prefer(f.v[t],f.win[t],f.dealIn[t],r.v[t],r.win[t],r.dealIn[t],retentionUnit())) {
                    f.v[t]=r.v[t]; f.win[t]=r.win[t];f.dealIn[t]=r.dealIn[t];
                    if(t==horizon) f.riichi=true;
                }
            }
        } else if(depth<=0) {
            f=generic(h,sh);
        } else {
            array<double,HORIZON> sum{}, winSum{},riskSum{};
            double rate=0;
            double tailFold=foldFloor(0);
            f.v[0]=tailFold;
            for(int t=0;t<34;++t) if(h.c[t]<4 && available(h,t)>0) {
                if(budgetExpired())return Future{};
                int u=available(h,t);double constrainedDraw=0;if(cx.constraintModel){constrainedDraw=conditionedDrawRate(t);if(constrainedDraw<=0)continue;}
                h.add(t);
                futureDraws.add(t);if(cx.constraintModel)futureWallDraws.add(t);
                if(shanten(h,cx.melds)<sh) {
                    Future best; best.v.fill(-1e20);array<retention::Frontier,HORIZON> options;
                    for(int d=0;d<34;++d) if(h.c[d]) {
                        h.add(d,-1);
                        if(shanten(h,cx.melds)==sh-1) {
                            U64 priorDiscards=futureDiscards;
                            futureDiscards|=U64(1)<<d;
                            Future child=eval(h,sh-1,depth-1);
                            futureDiscards=priorDiscards;
                            if(exhausted) { h.add(d);h.add(t,-1);futureDraws.add(t,-1);if(cx.constraintModel)futureWallDraws.add(t,-1);return Future{}; }
                            // This discard has NOT passed yet. Its future
                            // furiten history is valid only inside the child.
                            // Price ron now, including the continuation that
                            // cannot be earned if this particular tile deals in.
                            auto exposure=standing.enabled?futureExposure(d):FutureDiscardExposure{};
                            for(int k=0;k<HORIZON;++k) {
                                double value=exposure.value(child.v[k]);
                                options[k].addPolicy(value,exposure.win(child.win[k]),exposure.dealIn(child.dealIn[k]),d);
                            }
                        }
                        h.add(d);
                    }
                    for(int k=0;k<HORIZON;++k){int chosen=options[k].selected(retentionUnit());
                        if(chosen<0){exhausted=true;h.add(t,-1);futureDraws.add(t,-1);if(cx.constraintModel)futureWallDraws.add(t,-1);return Future{};}
                        best.v[k]=options[k].entries[chosen].base;best.win[k]=options[k].entries[chosen].win;best.dealIn[k]=options[k].entries[chosen].dealIn;
                    }
                    double q=cx.constraintModel?constrainedDraw:u/pool; // normal draw only; calls have distinct locked branches
                    rate+=q; f.uke+=u;
                    for(int k=0;k<HORIZON;++k) { sum[k]+=q*best.v[k]; winSum[k]+=q*best.win[k];riskSum[k]+=q*best.dealIn[k]; }

                }
                h.add(t,-1);
                futureDraws.add(t,-1);if(cx.constraintModel)futureWallDraws.add(t,-1);
            }
            if(rate>.85) { for(int k=0;k<HORIZON;++k) { sum[k]*=.85/rate; winSum[k]*=.85/rate;riskSum[k]*=.85/rate; } rate=.85; }
            auto calls=callContinuations(h,sh,depth);
            if(!calls.empty()) {
                int after=horizon==0 && futureDraws.n==0?cx.st.actor:cx.st.seat;
                int elapsed=horizon==0 && futureDraws.n==0?0:max(0,cx.st.remaining-tailDraws);
                auto tail=advanceCalls(calls,after,tailDraws,tailDraws,elapsed,notenDrawValue,0);
                if(retention::prefer(tailFold,0,0,tail.first,tail.second,tail.dealIn,retentionUnit())){f.v[0]=tail.first;f.win[0]=tail.second;f.dealIn[0]=tail.dealIn;}else{f.v[0]=tailFold;f.win[0]=0;}
            }
            for(int k=1;k<HORIZON;++k) {
                auto interval=cycleInterval(k,false);
                // Effective branches already paid their selected discard's
                // exposure. Retain the old approximation only for unexpanded
                // non-progress draws, and only if other players did not end
                // the hand before this next own discard.
                auto ordinaryExposure=averageExposure(.16,ownWinTime(k));
                if(standing.enabled) {
                    f.v[k]=interval.stopped[0]+interval.survival*((1-rate)*ordinaryExposure.value(f.v[k-1])+sum[k-1]);
                    f.win[k]=interval.survival*((1-rate)*ordinaryExposure.win(f.win[k-1])+winSum[k-1]);
                    f.dealIn[k]=interval.survival*((1-rate)*ordinaryExposure.dealIn(f.dealIn[k-1])+riskSum[k-1]);
                } else {
                    f.v[k]=interval.stopped[0]+interval.survival*((1-rate)*f.v[k-1]+sum[k-1])-cx.meanDanger*.16;
                    f.win[k]=interval.survival*((1-rate)*f.win[k-1]+winSum[k-1]);
                    f.dealIn[k]=interval.survival*((1-rate)*f.dealIn[k-1]+riskSum[k-1]);
                }
                if(!calls.empty()) {
                    int draws=cycleDistance(k),elapsed=cycleElapsed(k);
                    bool root=futureDraws.n==0 && k==horizon;
                    int remaining=root?cx.st.remaining:4*k+tailDraws;
                    if(remaining<=cx.st.remaining && draws>0) {
                        double own=(1-rate)*ordinaryExposure.value(f.v[k-1])+sum[k-1];
                        double win=(1-rate)*ordinaryExposure.win(f.win[k-1])+winSum[k-1];
                        double risk=(1-rate)*ordinaryExposure.dealIn(f.dealIn[k-1])+riskSum[k-1];
                        auto branch=advanceCalls(calls,root?cx.st.actor:cx.st.seat,draws-1,remaining,elapsed,own,win,risk);
                        f.v[k]=branch.first;f.win[k]=branch.second;f.dealIn[k]=branch.dealIn;
                    }
                }
                if(retention::prefer(f.v[k],f.win[k],f.dealIn[k],foldFloor(k),0,0,retentionUnit())) { f.v[k]=foldFloor(k);f.win[k]=0;f.dealIn[k]=0; }
            }
        }
        if(!exhausted)cache.emplace(key,f); return f;
    }
    double futureDanger(int tile) const {
        double total=0;
        bool survived=(rootOut>=0 && norm(rootOut)==tile) || (futureDiscards&(U64(1)<<tile));
        for(int who=0;who<4;++who) if(who!=cx.st.seat) {
            // A tile already passed after the current riichi is genbutsu to
            // that fixed hand. A silent hand can change its waits later.
            if(survived && cx.st.pl[who].riichi) continue;
            total+=cx.dangerByOpponent[who][tile];
        }
        return total;
    }
    FutureDiscardExposure averageExposure(double factor,int elapsed=0,bool committed=false) const {
        FutureDiscardExposure out;
        if(standing.enabled) {
            const auto& projected=cx.futureDrawExposure[clamp(elapsed+timeOffset,0,TemporalOtherWins::MAX_TIME-1)][committed?1:0];
            out.survival=1-factor*(1-projected.survival);out.loss=factor*projected.loss;
        } else out.loss=cx.meanDanger*factor;
        return out;
    }
    FutureDiscardExposure futureExposure(int tile) const {
        FutureDiscardExposure out;array<double,4> probability{};
        bool passed=(rootOut>=0 && norm(rootOut)==tile) || (futureDiscards&(U64(1)<<tile));
        for(int p=0;p<4;++p)if(p!=cx.st.seat && !(passed && cx.st.pl[p].riichi)) {
            probability[p]=cx.ronProbability[p][tile];out.loss+=cx.dangerByOpponent[p][tile];
        }
        out.survival=surviveImmediateRon(probability,cx.st.seat);return out;
    }
    Future improve(Hand& h,int sh,bool* complete=nullptr,int shapeDepth=1) {
        dt::SearchScope telemetryScope;dt::improveCall();
        if(!abortable)checkActionDeadline();
        if(complete) *complete=false;
        if(budgetExpired())return Future{};
        U64 improvementKey=searchKey(h,32+shapeDepth);
        auto found=improvementCache.find(improvementKey);
        if(found!=improvementCache.end()){if(complete)*complete=true;return found->second;}
        enterNode();++shapeNodes;dt::shape();
        Future base=sh==0?ready(h,false):eval(h,sh,sh);
        struct Branch {
            int draw=-1;
            double probability=0;
            array<double,HORIZON> gain{},winDelta{},riskDelta{};
            array<int,HORIZON> discard{};
        };
        vector<Branch> branches; branches.reserve(34);
        for(int t=0;t<34;++t) if(available(h,t)>0 && h.c[t]<4) {
            if(budgetExpired()) return base;
            int u=available(h,t);double constrainedDraw=0;if(cx.constraintModel){constrainedDraw=conditionedDrawRate(t);if(constrainedDraw<=0)continue;} h.add(t); futureDraws.add(t);if(cx.constraintModel)futureWallDraws.add(t);
            if(shanten(h,cx.melds)==sh) {
                Future best=base;array<retention::Frontier,HORIZON> options;
                for(int k=0;k<HORIZON;++k)options[k].addPolicy(base.v[k],base.win[k],base.dealIn[k],t);
                array<int,HORIZON> bestDiscard; bestDiscard.fill(t);
                for(int d=0;d<34;++d) if(d!=t && h.c[d]) {
                    h.add(d,-1);
                    if(shanten(h,cx.melds)==sh) {
                        U64 priorDiscards=futureDiscards;
                        futureDiscards|=U64(1)<<d;
                        bool childComplete=true;
                        Future child=shapeDepth<=1?eval(h,sh,sh):improve(h,sh,&childComplete,shapeDepth-1);
                        futureDiscards=priorDiscards;
                        if(exhausted || !childComplete) { h.add(d);h.add(t,-1);futureDraws.add(t,-1);if(cx.constraintModel)futureWallDraws.add(t,-1);return base; }
                        // Both paths already pay the same generic future-risk
                        // allowance. Charge only the added risk of changing
                        // the hand rather than discarding this hypothetical draw.
                        // Do not award a safety rebate: the fallback recurrence
                        // did not explicitly pay that specific discard cost.
                        if(standing.enabled) {
                            double extra=max(0.0,futureDanger(d)-futureDanger(t));
                            // One additional exposure applies to all three channels.
                            // It replaces no other loss term and grants no unpriced safety rebate.
                            double oldSurvive=futureExposure(t).survival,newSurvive=futureExposure(d).survival;
                            FutureDiscardExposure change;change.survival=oldSurvive>0?min(1.,newSurvive/oldSurvive):1.;change.loss=extra;
                            for(int k=0;k<HORIZON;++k){child.v[k]=change.value(child.v[k]);child.win[k]=change.win(child.win[k]);child.dealIn[k]=change.dealIn(child.dealIn[k]);}
                        }
                        for(int k=0;k<HORIZON;++k)options[k].addPolicy(child.v[k],child.win[k],child.dealIn[k],d);
                    }
                    h.add(d);
                }
                for(int k=0;k<HORIZON;++k){int chosen=options[k].selected(retentionUnit());
                    if(chosen<0){exhausted=true;h.add(t,-1);futureDraws.add(t,-1);if(cx.constraintModel)futureWallDraws.add(t,-1);return base;}
                    best.v[k]=options[k].entries[chosen].base;best.win[k]=options[k].entries[chosen].win;best.dealIn[k]=options[k].entries[chosen].dealIn;bestDiscard[k]=options[k].labels[chosen];
                }
                if(debugPolicy && horizon>=1 && retention::prefer(base.v[horizon-1],base.win[horizon-1],base.dealIn[horizon-1],best.v[horizon-1],best.win[horizon-1],best.dealIn[horizon-1],retentionUnit())) {
                    int d=bestDiscard[horizon-1];
                    cerr<<json({{"improvement_branch",true},{"root_discard",name(rootOut)},
                        {"draw",name(t)},{"discard",name(d)},{"horizon",horizon},
                        {"copies",u},{"base_value",base.v[horizon-1]},{"child_value",best.v[horizon-1]},
                        {"discard_risk",futureDanger(d)},{"draw_discard_risk",futureDanger(t)},
                        {"incremental_risk",futureDanger(d)-futureDanger(t)},
                        {"risk_surcharge",standing.enabled?max(0.0,futureDanger(d)-futureDanger(t)):0.0}}).dump()<<'\n';
                }
                Branch branch; branch.draw=t; branch.probability=cx.constraintModel?constrainedDraw:u/pool;
                branch.discard=bestDiscard;
                for(int k=0;k<HORIZON;++k) if(retention::prefer(base.v[k],base.win[k],base.dealIn[k],best.v[k],best.win[k],best.dealIn[k],retentionUnit())) {
                    branch.gain[k]=best.v[k]-base.v[k];
                    branch.winDelta[k]=best.win[k]-base.win[k];branch.riskDelta[k]=best.dealIn[k]-base.dealIn[k];
                }
                branches.push_back(branch);
            }
            h.add(t,-1);
            futureDraws.add(t,-1);if(cx.constraintModel)futureWallDraws.add(t,-1);
        }
        if(exhausted) return base;
        double exitRate=min(.75,base.uke/pool*(sh==0?1.65:1.0));
        Future out=base; double delta=0,wd=0,rd=0,greedy=0;
        json optionTrace=json::array();
        vector<ImprovementOffer> offers; offers.reserve(34);
        for(int k=1;k<HORIZON;++k) {
            offers.clear();
            for(const auto& branch:branches)
                offers.push_back({branch.probability,branch.gain[k-1],branch.winDelta[k-1],branch.riskDelta[k-1]});
            double waiting=delta,waitingWin=wd,waitingRisk=rd;
            auto step=chooseImprovement(delta,wd,rd,exitRate,cycleInterval(k,false).survival,offers,base.v[k-1],base.win[k-1],base.dealIn[k-1],retentionUnit());
            delta=step.value; wd=step.winDelta;rd=step.riskDelta;
            out.v[k]+=delta; out.win[k]+=wd;out.dealIn[k]+=rd;
            if(debugPolicy && k<=horizon) {
                double oldGain=0;
                json options=json::array();
                for(const auto& branch:branches) if(abs(branch.gain[k-1])>1e-12 || abs(branch.winDelta[k-1])>1e-12 || abs(branch.riskDelta[k-1])>1e-12) {
                    double q=branch.probability*step.scale;
                    oldGain+=q*branch.gain[k-1];
                    options.push_back({{"draw",name(branch.draw)},{"discard",name(branch.discard[k-1])},
                        {"probability",q},{"gain",branch.gain[k-1]},{"win_delta",branch.winDelta[k-1]},{"risk_delta",branch.riskDelta[k-1]},
                        {"accept",retention::prefer(base.v[k-1]+waiting,base.win[k-1]+waitingWin,base.dealIn[k-1]+waitingRisk,base.v[k-1]+branch.gain[k-1],base.win[k-1]+branch.winDelta[k-1],base.dealIn[k-1]+branch.riskDelta[k-1],retentionUnit())}});
                }
                greedy=cycleInterval(k,false).survival*((1-exitRate-step.positiveMass*step.scale)*greedy+oldGain);
                optionTrace.push_back({{"draws_left",k},{"waiting_value",waiting},{"option_value",delta},
                    {"greedy_value",greedy},{"accepted_mass",step.acceptedMass},{"waited_mass",step.waitedMass},
                    {"scale",step.scale},{"options",options}});
            }
        }
        if(debugPolicy) cerr<<json({{"improvement_option",true},{"root_discard",name(rootOut)},
            {"shanten",sh},{"horizon",horizon},{"exit_rate",exitRate},{"survival",cx.survival},
            {"steps",optionTrace}}).dump()<<'\n';
        if(sh==0 && canPlanRiichi()) {
            Future riichi=ready(h,true);
            for(int k=0;k<HORIZON;++k) if(k>=1 && retention::prefer(out.v[k],out.win[k],out.dealIn[k],riichi.v[k],riichi.win[k],riichi.dealIn[k],retentionUnit())) {
                out.v[k]=riichi.v[k]; out.win[k]=riichi.win[k];out.dealIn[k]=riichi.dealIn[k];
                if(k==horizon) out.riichi=true;
            }
        }
        if(budgetExpired())return base;
        if(complete) *complete=true;
        improvementCache.emplace(improvementKey,out);
        return out;
    }
};

#include "call_forecast.hpp"
#include "parallel_search_common.hpp"
#include "ready_ordered.hpp"

struct Choice { int raw=-1, sh=9, uke=0; double score=-1e30; bool riichi=false; double win=0, offense=0,dealIn=0,defenseRisk=0;
    double shapeAdjustment=0,redAdjustment=0,shantenAdjustment=0,reserveAdjustment=0; int depth=0; bool winAvailable=true;
    bool estimateComplete=false,defenseAvailable=false,attackAlreadyFold=false,nagashiZeroWinCertified=false;
    int nagashiOwnDraws=0;
    double defenseScore=-1e30,defenseOff=-1e30,nagashiOff=-1e30;
};

#ifdef V48_SEARCH_DIAGNOSTIC
// Compiled only into the local fault-injection harness. These controls may
// shorten work; they cannot extend the production node or time budgets.
int searchLayerCeiling=9, searchAbortLayer=-1, searchAbortAfter=-1;
bool searchReverse=false;
#endif

#ifdef V63_SEARCH_DIAGNOSTIC
bool adaptiveEnabled=true,adaptiveReverse=false;
int adaptiveAbortAfter=-1;
#endif
// Only the already complete candidate layer can update the deadline fallback.
#include "retention_discard.hpp"
#include "parallel_discard.hpp"
// Compare legal discards using hand progress, score, and public information.
Choice chooseDiscard(Hand h,Context& cx,const vector<bool>& forbidden={},bool deep=true) {
    vector<Choice> cs; int minsh=9,maxsh=0;
    auto searchUtility=[&](const Choice& c){return c.raw<0?-1e30:retention::objective({c.score,cx.immediateSurvival[norm(c.raw)]*c.win,true,true,1-cx.immediateSurvival[norm(c.raw)]+cx.immediateSurvival[norm(c.raw)]*c.dealIn},retentionUnit());};
    for(int t=0;t<34;++t) if(h.c[t] && (forbidden.empty() || !forbidden[t])) {
        int raw=cx.st.actual(t); if(raw<0) continue;
        h.add(t,-1); int s=shanten(h,cx.melds); h.add(t);
        minsh=min(minsh,s); maxsh=max(maxsh,s); cs.push_back({raw,s,0,0,false});
    }
    Evaluator ev(cx);
    Choice best;
    for(auto &c:cs) {
        int t=norm(c.raw); h.add(t,-1);
        ev.rootOut=c.raw;
        // Establish a complete inexpensive fallback before any deeper pass.
        // Ready hands already have concrete waits at depth zero.
        Future f=ev.eval(h,c.sh,0);
        c.uke=f.uke; c.offense=f.v[ev.horizon]; c.win=f.win[ev.horizon];c.dealIn=f.dealIn[ev.horizon]; c.riichi=f.riichi;
        if(!cx.st.closed() && c.sh>0) c.offense=ev.withPotential(c.offense,openPotential(h,cx));
        c.shapeAdjustment=shapeValue(h,cx)*3.0;c.redAdjustment=-(c.raw>=34?25:0);
        c.shantenAdjustment=-max(0,c.sh-minsh)*(cx.threat>.6?160.0:420.0)*min(1.0,cx.st.remaining/24.0);
        c.score=cx.actionValue(t,c.offense)+c.shapeAdjustment+c.redAdjustment;
        c.score+=c.shantenAdjustment;
        // A safe reserve lets a distant hand stop pushing on a later turn.
        if(cx.threat>.45 && c.sh>0) {
            double safest=1e30;
            for(int u=0;u<34;++u) if(h.c[u]) safest=min(safest,cx.danger[u]);
            if(safest<1e20){c.reserveAdjustment=-min(250.0,safest)*.18;c.score+=c.reserveAdjustment;}
        }
        double reserve=0;if(cx.threat>.45 && c.sh>0){double safest=1e30;for(int u=0;u<34;++u)if(h.c[u])safest=min(safest,cx.danger[u]);if(safest<1e20)reserve=min(250.0,safest)*.18;}
        if(standing.enabled && !cx.st.pl[cx.st.seat].riichi){c.defenseAvailable=true;c.defenseOff=ev.foldFloor(ev.horizon);
            c.defenseScore=cx.actionValue(t,c.defenseOff)-reserve;}
        c.nagashiOff=ev.nagashiValue(h,c.raw);c.nagashiOwnDraws=ev.horizon;c.nagashiZeroWinCertified=retention::nagashiZeroWinCertificate(c.sh,ev.horizon);c.estimateComplete=true;
#ifdef RETENTION_FAULT_TEST
        retentionFaultPoint(2);
#endif
        c.attackAlreadyFold=c.defenseAvailable && abs(c.offense-c.defenseOff)<1e-9 && c.win==0 && !c.riichi;
        if(ev.exhausted)throw ActionBudgetExpired{};
        h.add(t);
        if(debugPolicy) cerr<<json({{"tile",name(c.raw)},{"sh",c.sh},{"depth",0},{"uke",c.uke},{"off",c.offense},{"risk",cx.danger[t]},{"p_ron",1-cx.immediateSurvival[t]},{"lost_continuation",c.offense-cx.survivingContinuation(t,c.offense)},{"surviving_offense",cx.survivingContinuation(t,c.offense)},{"score",c.score},{"win",c.win},{"riichi",c.riichi},{"nodes",ev.nodes}}).dump()<<'\n';
        if(searchUtility(c)>searchUtility(best)) best=c;
    }
    rememberDiscard(cs,cx);
    if(retentionBaselineMode)return retainedDiscardChoice();
    DiscardParallel search(cx,ev,h);
    // Every legal discard receives the same number of concrete improvement
    // layers, up to reaching ready. When any candidate is <= two shanten,
    // try to reach concrete waits for all candidates (at most three layers).
    // A distant hand keeps the previous one/two-layer work limit.
    int targetDepth=deep?1:0;
    if(deep && minsh<=2) targetDepth=min(3,maxsh);
#ifndef V32_NO_THREE_SHANTEN
    else if(deep && forbidden.empty() && minsh==3 && cx.st.remaining>=16) targetDepth=2;
#endif
#ifdef V48_SEARCH_DIAGNOSTIC
    targetDepth=min(targetDepth,searchLayerCeiling);
#endif
#ifdef PARALLEL_REGRESSION
    targetDepth=min(targetDepth,parallelTestDepth);
#endif
    ev.abortable=true;
    for(int depth=1;depth<=targetDepth;++depth) {
        vector<int> selected;for(int i=0;i<int(cs.size());++i)if(cs[i].depth<min(depth,cs[i].sh))selected.push_back(i);
        if(!search.refine(cs,selected,depth,0)){ev.exhausted=true;break;}
    }
    best=Choice{};
    for(auto &c:cs) if(searchUtility(c)>searchUtility(best)) best=c;
#ifndef NO_UPGRADE
    if(deep && !ev.exhausted && minsh<=IMPROVE_SHANTEN && cx.st.remaining>=6
#ifdef PARALLEL_REGRESSION
       && !parallelTestNoShape
#endif
    ) {
        // Filter before taking the top three: an ineligible discard must not
        // consume a slot that a legal same-shanten improvement could use.
        vector<int> order;
        for(int i=0;i<(int)cs.size();++i) if(cs[i].sh==minsh && searchUtility(cs[i])+.65*retentionUnit()>=searchUtility(best)) order.push_back(i);
        stable_sort(order.begin(),order.end(),[&](int a,int b) { return searchUtility(cs[a])>searchUtility(cs[b]); });
        if(order.size()>3) order.resize(3);
        if(debugPolicy) {
            json selected=json::array();for(int i:order) selected.push_back(name(cs[i].raw));
            cerr<<json({{"upgrade_candidates",selected}}).dump()<<'\n';
        }
        // Commit a whole shortlist together. A timeout must not award extra
        // lookahead only to whichever tile happened to be enumerated first.
        bool wholePass=search.refine(cs,order,0,1,true,true);
        if(debugPolicy) cerr<<json({{"upgrade_complete",wholePass},{"min_shanten",minsh},{"nodes",ev.nodes}}).dump()<<'\n';
        best=Choice{};
        for(auto &c:cs) if(searchUtility(c)>searchUtility(best)) best=c;
    }
#endif
    #include "adaptive_discard_search.hpp"
    rememberDiscard(cs,cx);best=retainedDiscardChoice();
    traceDiscardProbabilities(retainedDecision,ev.horizon,ev.tailDraws);
    if(!retentionBaselineMode)cerr<<json({{"search_summary","discard"},{"nodes",ev.nodes},{"shape_nodes",ev.shapeNodes},
        {"min_shanten",minsh},{"candidates",cs.size()},{"exhausted",ev.exhausted},
        {"seconds",chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()},
        {"chosen",name(best.raw)}}).dump()<<'\n';

    return best;
}

double openPotential(const Hand& h,Context &cx) {
    if(cx.st.closed()) return 1;
    bool simpleMelds=true, tripMelds=true;
    int suitMask=0;
    for(auto &m:cx.st.pl[cx.st.seat].melds) {
        if(m.kind && (m.base()>=31 || m.base()==27+cx.st.seat || m.base()==27+cx.st.round)) return 1;
        tripMelds &= m.kind!=0;
        for(int t:m.tiles) { t=norm(t); simpleMelds&=!terminal(t); if(t<27) suitMask|=1<<(t/9); }
    }
    double best=0;
    int outside=0,pairs=0;
    for(int t=0;t<34;++t) if(h.c[t]) {
        if(t>=31 || t==27+cx.st.seat || t==27+cx.st.round) {
            if(h.c[t]>=3) return 1;
            if(h.c[t]==2 && cx.unseen[t]) best=max(best,cx.unseen[t]>=2?.76:.55);
        }
        if(terminal(t)) outside+=h.c[t];
        if(t<27) suitMask|=1<<(t/9);
        pairs+=h.c[t]>=2;
    }
    if(simpleMelds) best=max(best,pow(.58,outside));
    if(suitMask==0 || !(suitMask&(suitMask-1))) return 1;
    if(tripMelds && pairs>=5-cx.melds) best=max(best,.85);
    return best;
}

struct CallOption { string action; int a,b; vector<bool> ban; };
#include "call_diagnostics.hpp"
#include "call_search.hpp"
string considerCall(State &st,const string &va) {
    if(debugPolicy)for(const auto& part:split(va,',')) {
        auto tokens=words(part);
        if(!tokens.empty() && tokens[0]=="GANG")traceUnestimatedCall("GANG","daiminkan_not_in_search_policy");
    }
    if(st.pl[st.seat].riichi || st.remaining<=0 || st.lastPlayer==st.seat) {
        dt::reason(st.pl[st.seat].riichi?"already_riichi_pass":st.remaining<=0?"wall_exhausted":"own_discard_event");
        if(debugPolicy)traceUnestimatedCall("PASS",st.pl[st.seat].riichi?"already_riichi":st.remaining<=0?"no_live_tiles":"own_discard_event");
        return "PASS";
    }
    int called=st.lastDiscard, n=norm(called);
    vector<CallOption> options;
    auto actualPreferRed=[&](int t) {
        if(t<27 && t%9==4 && st.raw[34+t/9]) return 34+t/9;
        return st.actual(t);
    };
    if(va.find("PENG")!=string::npos && st.hand.c[n]>=2) {
        int a=actualPreferRed(n), b=(a>=34?n:st.raw[n]>=2?n:34+n/9);
        vector<bool> ban(34); ban[n]=true;
        options.push_back({"PENG",a,b,ban});
    }
    if(va.find("CHI")!=string::npos && (st.seat-st.lastPlayer+4)%4==1 && n<27) {
        int rank=n%9,base=n/9*9;
        for(int start=max(0,rank-2);start<=min(6,rank);++start) {
            vector<int> need;
            for(int r=start;r<start+3;++r) if(r!=rank) need.push_back(base+r);
            if(!st.hand.c[need[0]] || !st.hand.c[need[1]]) continue;
            vector<bool> ban(34); ban[n]=true;
            if(rank==start && rank<6) ban[n+3]=true;
            if(rank==start+2 && rank>=3) ban[n-3]=true;
            options.push_back({"CHI",actualPreferRed(need[0]),actualPreferRed(need[1]),ban});
        }
    }
    if(options.empty()) {
        dt::reason("no_eligible_call");
        if(debugPolicy)traceUnestimatedCall("PASS","no_generated_call_option");
        return "PASS";
    }
    return compareCallsTogether(st,options);
}

string considerKan(State& st,const string &va,const Choice &normal) {
    if(retentionBaselineMode)return "";
    if(st.pl[st.seat].riichi || st.remaining<5 || normal.sh>2 ||
       dt::startGate(chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()>START_IMPROVE_SECONDS)) {
        if(debugPolicy)traceUnestimatedKanOffers(va,"kan_policy_or_start_budget_gate");
        return "";
    }
    int kanCount=0;
    for(int p=0;p<4;++p) {
        if(p!=st.seat && st.pl[p].riichi) {
            if(debugPolicy)traceUnestimatedKanOffers(va,"opponent_riichi_policy_gate");
            return "";
        }
        for(auto &m:st.pl[p].melds) kanCount+=m.kind==2;
    }
    if(kanCount>=3) {
        if(debugPolicy)traceUnestimatedKanOffers(va,"kan_count_policy_gate");
        return "";
    }
    vector<pair<string,int>> options;
    for(auto part:split(va,',')) {
        auto w=words(part); if(w.empty()) continue;
        if(w[0]=="ANGANG") {
            for(int t=0;t<34;++t) if(st.hand.c[t]==4) {
                bool allowed=w.size()==1;
                for(size_t k=1;k<w.size();++k) if(norm(tile(w[k]))==t) allowed=true;
                if(allowed) options.push_back({"ANGANG",st.actual(t)});
            }
        } else if(w[0]=="BUGANG") {
            for(auto &m:st.pl[st.seat].melds) if(m.kind==1 && st.hand.c[m.base()]) {
                int t=m.base(); bool allowed=w.size()==1;
                for(size_t k=1;k<w.size();++k) if(norm(tile(w[k]))==t) allowed=true;
                if(allowed) options.push_back({"BUGANG",st.actual(t)});
            }
        }
    }
    vector<RetentionAlternative> candidates=retainedDecision.alternatives;bool complete=true;int valued=0;
    for(auto o:options) {
        if(dt::startGate(chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()>START_IMPROVE_SECONDS)) {
            if(debugPolicy)traceUnestimatedKan(o.first,o.second,"start_budget_exhausted");
            complete=false;break;
        }
        State next=st; int n=norm(o.second);
        if(o.first=="ANGANG") {
            Meld m; m.kind=2; m.open=false;
            if(n<27 && n%9==4) m.tiles={n,n,n,34+n/9}; else m.tiles={n,n,n,n};
            for(int t:m.tiles) { next.add(t,-1); ++next.publicSeen[norm(t)]; }
            next.pl[st.seat].melds.push_back(m);
        } else {
            next.add(o.second,-1); ++next.publicSeen[n];
            for(auto &m:next.pl[st.seat].melds) if(m.kind==1 && m.base()==n) { m.kind=2; m.tiles.push_back(o.second); break; }
        }
        if(standing.enabled) next.replacementForecast=true;
        else { next.remaining+=4;next.allowEndWallForecast=false; }
        Context cx(next); cx.assessThreats(); Hand h=next.hand;
        int sh=shanten(h,cx.melds);
        if(sh>normal.sh) {
            if(debugPolicy)traceUnestimatedKan(o.first,o.second,"shanten_loss_filter",&cx);
            continue;
        }
        Evaluator ev(cx);ev.abortable=true;Future f=ev.eval(h,sh,sh<=2?2:1);
        if(ev.budgetExpired()) { // Reject a kan result completed after the shared deadline.
            if(debugPolicy)traceUnestimatedKan(o.first,o.second,"search_budget_exhausted",&cx);
            complete=false;break;
        }
        double continuation=ev.withPotential(f.v[ev.horizon],1.055);
        double value=(o.first=="BUGANG"?cx.actionValue(n,continuation):continuation)-220-cx.threat*200;
        if(debugPolicy)traceComputedKan(o.first,o.second,cx,continuation,value,normal.score);
        RetentionAlternative candidate;candidate.action=o.first+" "+name(o.second);candidate.kind=o.first=="BUGANG"?"added_kan_proxy":"concealed_kan_unknown_robbery";
        candidate.tileName=name(o.second);candidate.raw=o.second;candidate.sh=sh;candidate.uke=f.uke;candidate.base=value;candidate.off=continuation;candidate.switchCost=180;
        candidate.conditionalWin=f.win[ev.horizon];candidate.conditionalRisk=f.dealIn[ev.horizon];candidate.survival=o.first=="BUGANG"?cx.immediateSurvival[n]:1;
        candidate.loss=o.first=="BUGANG"?cx.danger[n]:0;candidate.hasImmediateDiscard=o.first=="BUGANG";candidate.known=o.first=="BUGANG";candidate.immediateRiskKnown=o.first=="BUGANG";
        candidate.origin=int(candidates.size())+1000;candidates.push_back(candidate);++valued;
    }
    if(complete && valued && rememberRetention(RetentionDecision(move(candidates),"discard_and_kan")))return deadlineResponse;
    return "";
}

string actImpl(State& st,const json& q) {
    infer::actionCache.clear();
    prepareStanding(st);
    if(debugPolicy && standing.enabled) cerr<<json({{"match_context",positionOf(st).explain()}}).dump()<<'\n';
    string va=q.contains("validact") && q["validact"].is_string()?q["validact"].get<string>():"";
    if(va.find("TSUMO")!=string::npos || va.find("RONG")!=string::npos) {dt::reason(va.find("TSUMO")!=string::npos?"tsumo":"ron");return "HU";}
    if(st.event!="DRAW" || st.actor!=st.seat) {
        if(st.event=="PLAY" || st.event=="LIZHI" || st.event=="CHI" || st.event=="PENG") return considerCall(st,va);
        dt::reason(st.event=="INIT"?"initialization":"no_action_required");return "PASS";
    }
    if(st.pl[st.seat].riichi) {dt::reason("already_riichi_forced_discard");return "PLAY "+name(st.drawn);}
    Context cx(st);
    cx.assessThreats();
    Choice c=chooseDiscard(st.hand,cx);
    if(c.raw<0) {dt::reason("no_discard_candidate");return "PLAY "+name(st.drawn);}
    if(va.find("GANG")!=string::npos) {
        string kan=considerKan(st,va,c); if(!kan.empty()) return kan;
    }
    bool allowed=false;
    for(auto part:split(va,',')) {
        auto w=words(part);
        if(!w.empty() && w[0]=="LIZHI") {
            if(w.size()==1) allowed=true;
            for(size_t i=1;i<w.size();++i) if(tile(w[i])==c.raw) allowed=true;
        }
    }
    return string(allowed && c.riichi && canDeclareRiichi(st)?"LIZHI ":"PLAY ")+name(c.raw);
}
string act(State& st,const json& q){
    deadlineValidActions=q.contains("validact") && q["validact"].is_string()?q["validact"].get<string>():"";
    deadlineResponse=st.event=="DRAW"&&st.actor==st.seat?"PLAY "+name(st.drawn):"PASS";
    deadlineSnapshot=nullptr;retainedDecision=RetentionDecision{};retentionBaselineMode=false;
#ifdef RETENTION_FAULT_TEST
    retentionFaultCounter=0;
#endif
    const bool trace=debugPolicy;enforceActionDeadline=true;
    struct Reset{bool debug;~Reset(){enforceActionDeadline=false;retentionBaselineMode=false;debugPolicy=debug;}} reset{trace};
    auto finish=[&](const string& answer){debugPolicy=trace;retentionBaselineMode=false;traceRetentionFinal(answer);return answer;};
    try{
        // Complete a coherent low-resolution model pass before optional full
        // inference or deep search. The common request clock is never reset.
        retentionBaselineMode=true;debugPolicy=false;string baseline=actImpl(st,q);
        retentionBaselineMode=false;debugPolicy=trace;
        if(!retainedDecision.valid())return finish(baseline); // terminal/forced/no generated alternative
#ifdef RETENTION_FAULT_TEST
        retentionFaultPoint(1);
#endif
        if(sharedSearchStopped())throw ActionBudgetExpired{};
        return finish(actImpl(st,q));
    }catch(const ActionBudgetExpired&){
        debugPolicy=trace;retentionBaselineMode=false;dt::outerFallback();
        if(debugPolicy)cerr<<json({{"budget_fallback",true},{"response",deadlineResponse},{"estimates",deadlineSnapshot},
            {"estimate_status",retainedDecision.valid()?"last_complete_soft_pass":"no_certified_candidate_set"},
            {"complete_candidate_set",retainedDecision.valid()},{"seconds",chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()},
            {"search_budget_seconds",SEARCH_SECONDS}}).dump()<<'\n';
        if(!retainedDecision.valid())dt::r.error="retention baseline could not complete before deadline";
        return finish(deadlineResponse);
    }catch(const exception& e){debugPolicy=trace;retentionBaselineMode=false;dt::r.error=e.what();dt::interrupted();return finish(deadlineResponse);}
}

}

int main(int argc,char**argv) {
    ios::sync_with_stdio(false); cin.tie(nullptr);
    try {prt::runtime.configure(argc,argv);} catch(const prt::RuntimeError& e) {
        cerr<<json({{"type","parallel_runtime_error"},{"operation",e.operation},{"win32_error",e.code},{"error",e.what()}}).dump()<<'\n';return 3;
    } catch(const exception& e){cerr<<json({{"type","parallel_runtime_error"},{"error",e.what()}}).dump()<<'\n';return 3;}
    try { mj::standing.loadFile(); }
    catch(const exception& e) { cerr<<"invalid match context: "<<e.what()<<'\n'; return 2; }
    cerr<<(mj::standing.enabled?"match_context=public-file":"match_context=absent; legacy-single-hand-mode")<<'\n';
    mj::debugPolicy=prt::runtime.analyze;
    if(argc>1 && string(argv[1])=="--kokushi-model") {
        mj::initTables();string line;
        while(getline(cin,line))cout<<mj::infer::run(json::parse(line)).dump()<<endl;
        return 0;
    }
    if(argc>1 && string(argv[1])=="--hazard-projection") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line);mj::TemporalOtherWins model;
            array<mj::PublicHazardPlayer,4> players{};
            for(int p=0;p<4;++p) {
                auto x=q.at("players")[p];auto &out=players[p];
                out.locked=x.value("locked",false);out.open=x.value("open",false);out.canDeclare=x.value("can_declare",false);
                out.turn=x.value("turn",0);out.melds=x.value("melds",0);out.ready=x.at("ready");
                array<double,2> value=q.value("outcome_value",array<double,2>{{0,0}});
                model.addTsumo(p,1,value);
                for(int payer=0;payer<4;++payer) if(payer!=p)model.addRon(p,payer,1,value);
            }
            int self=q.at("self"),actor=q.at("actor"),remaining=q.at("remaining");
            model.configure(self,actor,remaining,q.value("replacement",false),players);
            json projections=json::array(),intervals=json::array();
            for(int time:q.value("times",vector<int>{0,4,12}))projections.push_back({{"time",time},{"projection",model.project(time).explain()}});
            for(auto x:q.value("intervals",json::array())) {
                auto out=model.interval(self,x.at("after_actor"),x.at("draws"),x.value("target_survival",-1.),x.value("elapsed",0));
                intervals.push_back({{"survival",out.survival},{"stopped",out.stopped}});
            }
            cout<<json({{"model",model.explain()},{"projections",projections},{"intervals",intervals}}).dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--other-win-tree") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line);mj::OtherWinModel model;
            int self=q.at("self");
            for(auto actor:q.at("actors")) model.add(actor.at("seat"),actor.at("weight"),actor.at("value"),actor.at("committed_value"));
            model.finish(q.at("survival"));
            json intervals=json::array(),steps=json::array();
            for(auto i:q.value("intervals",json::array())) {
                auto out=model.interval(self,i.at("after_actor"),i.at("draws"),i.value("target_survival",-1.));
                intervals.push_back({{"survival",out.survival},{"stopped",out.stopped}});
            }
            double value=q.value("draw_value",0.),fold=q.value("fold_draw_value",0.);
            for(auto step:q.value("steps",json::array())) {
                auto interval=model.interval(self,step.at("after_actor"),step.at("draws"),step.value("target_survival",-1.));
                bool committed=step.value("committed",false);
                double continuation=step.value("can_fold_after_draw",false)?max(value,fold):value;
                double p=step.at("own_win_probability");
                value=mj::stoppedContinuation(p,p*step.at("own_win_value").get<double>(),interval,continuation,committed,step.value("tax",0.));
                fold=interval.stopped[0]+interval.survival*fold;
                steps.push_back({{"value",value},{"fold",fold},{"continuation",continuation}});
            }
            json out={{"model",model.explain()},{"intervals",intervals},{"steps",steps}};
            if(q.contains("potential")) out["scaled_value"]=mj::scaleAboveFold(value,fold,q.at("potential"));
            cout<<out.dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--other-win-settlement") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line);mj::standing.load(q.at("match_context"));
            int self=q.at("self"),winner=q.at("winner");mj::PositionValue pos(self,q.at("reached"));
            auto scores=pos.settled(winner,q.value("payer",-1),q.at("points"),q.at("tsumo"),q.value("own_future_riichi",false));
            int remaining=mj::standing.after(scores,winner);
            cout<<json({{"scores",scores},{"remaining_hands",remaining},{"own_rank",mj::standing.rank(scores,self)+1},
                {"value",mj::standing.value(scores,remaining,self)-pos.reference()},{"reference",pos.reference()}}).dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--draw-settlement") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line); mj::standing.load(q.at("match_context"));
            int self=q.at("self"); unsigned reached=q.value("reached",0u),ready=q.at("ready").get<unsigned>();
            mj::PositionValue pos(self,reached);
            auto result=pos.settleDraw(ready,q.value("own_future_riichi",false));
            cout<<json({{"before_award",result.beforeAward},{"scores",result.scores},
                {"future_hands",result.futureHands},{"unawarded_sticks",result.unawardedSticks},
                {"awarded_sticks",result.awardedSticks},{"recipient",result.recipient},
                {"value",pos.drawValue(ready,q.value("own_future_riichi",false))},
                {"unreturned_deposit_cost",pos.depositCost(ready&~(1u<<self))}}).dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--match-value") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line);
            mj::standing.load(q.at("match_context"));
            auto scores=q.at("scores").get<array<int,4>>();
            json values=json::array(), ranks=json::array();
            for(int who=0;who<4;++who) {
                values.push_back(mj::standing.value(scores,q.value("future_hands",0),who));
                ranks.push_back(mj::standing.rank(scores,who)+1);
            }
            cout<<json({{"values",values},{"ranks",ranks},
                       {"hands_after",mj::standing.after(scores,q.value("winner",-1),q.value("dealer_ready",false))},
                       {"return_points",mj::standing.returnPoints},{"end_threshold_points",mj::standing.endThreshold}}).dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--payout-model") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line);
            mj::OpponentPayouts distribution(q.at("mean").get<double>(),q.value("dealer",false));
            cout<<distribution.explain().dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--immediate-value") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line);
            double continuation=q.at("continuation").get<double>();
            double loss=q.at("expected_loss").get<double>();
            auto probabilities=q.at("ron_probabilities").get<array<double,4>>();
            double survive=mj::surviveImmediateRon(probabilities,q.at("self").get<int>());
            cout<<json({{"survival",survive},{"value",mj::exposedActionValue(continuation,survive,loss)},
                        {"old_value",continuation-loss},{"lost_continuation",(1-survive)*continuation}}).dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--improvement-option") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line);
            double delta=0,win=0,risk=0;
            json result=json::array();
            for(const auto& row:q.at("steps")) {
                vector<mj::ImprovementOffer> offers;
                for(const auto& offer:row.at("offers")) offers.push_back({offer.at("probability").get<double>(),
                    offer.at("gain").get<double>(),offer.value("win_delta",0.0),offer.at("risk_delta").get<double>()});
                auto step=mj::chooseImprovement(delta,win,risk,row.value("exit_rate",0.0),row.value("survival",1.0),offers,row.value("base_value",0.),row.at("base_win").get<double>(),row.at("base_risk").get<double>(),row.value("unit",1000.));
                delta=step.value; win=step.winDelta;risk=step.riskDelta;
                result.push_back({{"value",delta},{"win_delta",win},{"risk_delta",risk},{"scale",step.scale},
                                  {"accepted_mass",step.acceptedMass},{"waited_mass",step.waitedMass}});
            }
            cout<<json({{"steps",result}}).dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && (string(argv[1])=="--ura-model" || string(argv[1])=="--ura-payout")) {
        bool payout=string(argv[1])=="--ura-payout";
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line);
            mj::ExpectedBonuses distribution(q.at("groups").get<array<int,5>>(),q.at("indicators").get<int>(),
                                             q.value("ippatsu_probability",mj::IPPATSU_WIN_PRIOR));
            json out=distribution.explain();
            if(payout) {
                mj::standing.load(q.value("match_context",json()));
                bool tsumo=q.value("tsumo",false),dealer=q.value("dealer",false);
                int self=q.value("self",dealer?0:1),payer=q.value("payer",0);
                mj::PositionValue pos(self,q.value("reached",0u));
                // Inject abstract legal-score interpretations through the same
                // production aggregation used after hand decomposition.
                mj::State scoreState; scoreState.seat=dealer?0:1;
                mj::Hand emptyHand;
                mj::Scorer aggregate(emptyHand,scoreState,0,1,-1,&distribution);
                for(const auto& pattern:q.at("patterns"))
                    aggregate.accept(pattern.at("han").get<int>(),pattern.at("fu").get<int>(),tsumo,pattern.value("yakuman",0));
                aggregate.finishExpected();
                double money=0,utility=0,first=0; json branches=json::array();
                for(int extra=0;extra<=mj::ExpectedBonuses::MAX_EXTRA;++extra) {
                    double probability=distribution.extra[extra]; if(probability<=0) continue;
                    int payment=(tsumo?aggregate.bestTsumo:aggregate.bestRon)[extra];
                    money+=probability*payment;
                    json branch={{"extra_han",extra},{"probability",probability},{"points",payment}};
                    if(mj::standing.enabled) {
                        double u=pos.win(payment,tsumo,true,payer);
                        auto sc=pos.settled(self,payer,payment,tsumo,true);
                        int rank=mj::standing.rank(sc,self)+1;
                        utility+=probability*u; first+=probability*(rank==1);
                        branch["utility"]=u; branch["rank_after"]=rank;
                    }
                    branches.push_back(branch);
                }
                out["branches"]=branches; out["expected_points"]=money;
                out["production_expected_points"]=tsumo?aggregate.result.tsumo:aggregate.result.ron;
                if(mj::standing.enabled) { out["expected_utility"]=utility; out["first_place_probability"]=first; }
            }
            cout<<out.dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--ron-source-value") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line); int self=q.at("self").get<int>();
            auto locked=q.at("locked").get<array<bool,4>>();
            auto exposure=q.contains("opportunities")?q.at("opportunities").get<array<double,4>>():
                mj::normalDrawExposure(self,q.at("after_actor").get<int>(),q.at("draws").get<int>());
            auto weights=mj::ronSourceWeights(self,locked,exposure);
            json out={{"weights",weights},{"ordinary_opportunities",exposure}};
            if(q.contains("match_context")) {
                mj::standing.load(q.at("match_context"));
                unsigned mask=0; for(int p=0;p<4;++p) if(locked[p]) mask|=1u<<p;
                mj::PositionValue pos(self,mask); array<double,4> values{};
                double uniform=0;
                for(int p=0;p<4;++p) if(p!=self) {
                    values[p]=pos.win(q.at("ron_points").get<int>(),false,q.value("own_future_riichi",false),p);
                    uniform+=values[p]/3;
                }
                out["by_payer"]=values; out["uniform_value"]=uniform;
                out["weighted_value"]=mj::sourceWeightedRon(weights,values);
            }
            cout<<out.dump()<<endl;
        }
        return 0;
    }
    mj::initTables();
    try {prt::runtime.start([]{(void)mj::shcache.data();});}
    catch(const exception& e){cerr<<json({{"type","parallel_runtime_error"},{"error",e.what()}}).dump()<<'\n';return 3;}
    cerr<<json({{"type","parallel_runtime"},{"runtime",prt::runtime.evidence()}}).dump()<<'\n';
    if(prt::runtime.runtimeInfoOnly){auto evidence=prt::runtime.evidence();prt::runtime.shutdown();evidence["shutdown"]=prt::runtime.evidence();cout<<evidence.dump()<<'\n';return 0;}
    if(argc>1 && string(argv[1])=="--end-wall-probe") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line); mj::State st;
            if(q.contains("query")) {
                auto input=q.at("query"); mj::standing.load(input.value("match_context",json()));
                st.consume(input);
            } else {
                // Explicit mathematical fixture, never used by the game protocol.
                auto fixture=q.at("fixture"); mj::standing.load(q.value("match_context",json()));
                st.seat=fixture.at("self"); st.actor=fixture.at("actor");
                st.remaining=fixture.at("remaining"); st.round=fixture.value("round",0);
                st.replacementForecast=fixture.value("replacement_forecast",false);
                for(auto t:fixture.at("hand")) st.add(mj::tile(t.get<string>()),1);
                if(fixture.contains("indicators")) for(auto t:fixture["indicators"]) {
                    int raw=mj::tile(t.get<string>()); st.indicators.push_back(raw); ++st.dora[mj::doraNext(raw)];
                }
                if(fixture.contains("melds")) for(auto item:fixture["melds"]) {
                    mj::Meld m; m.kind=item.at("kind"); m.open=item.value("open",true);
                    for(auto t:item.at("tiles")) { int raw=mj::tile(t.get<string>()); m.tiles.push_back(raw); ++st.publicSeen[mj::norm(raw)]; }
                    st.pl[st.seat].melds.push_back(m);
                }
                if(fixture.contains("riichi_seats")) for(int p:fixture["riichi_seats"]) st.pl[p].riichi=true;
                if(fixture.contains("own_river")) for(auto t:fixture["own_river"]) {
                    int raw=mj::tile(t.get<string>()); st.pl[st.seat].river.push_back({raw,0,false}); ++st.publicSeen[mj::norm(raw)];
                }
            }
            mj::actionStarted=chrono::steady_clock::now();
            mj::prepareStanding(st); mj::Context cx(st); cx.assessThreats(); mj::Evaluator ev(cx);
            int out=q.contains("discard")?mj::tile(q.at("discard").get<string>()):-1;
            int win=mj::norm(mj::tile(q.at("win").get<string>()));
            bool riichi=q.value("riichi",st.pl[st.seat].riichi);
            mj::Hand hand=st.hand;
            if(out>=0) hand.add(mj::norm(out),-1);
            ev.rootOut=out;
            if(hand.n+cx.melds*3!=13) throw runtime_error("probe requires a post-discard hand");
            mj::Hand complete=hand; complete.add(win);
            if(mj::shanten(complete,cx.melds)!=-1) throw runtime_error("probe wait does not win structurally");
            auto describe=[](const mj::WinScore& s) {
                return json({{"ron",s.ron},{"tsumo",s.tsumo},{"ron_han",s.ronHan},{"ron_fu",s.ronFu},
                    {"tsumo_han",s.tsumoHan},{"tsumo_fu",s.tsumoFu},{"ron_by_payer",s.ronByPayer},{"tsumo_utility",s.tsumoUtility}});
            };
            json cached=json::array(),raw=json::array();
            for(bool last:{false,true,false}) {
                auto score=ev.cachedWinScore(complete,win,riichi,last);
                cached.push_back({{"last_normal_tile",last},{"score",describe(score)},{"cache_size",ev.handScores.size()}});
            }
            for(bool last:{false,true}) {
                mj::Scorer scorer(complete,st,win,riichi?1:0,out,nullptr,last); scorer.expected=false;
                raw.push_back(describe(scorer.run()));
            }
            auto changed=ev.ready(hand,riichi);
            st.allowEndWallForecast=false;
            auto ordinary=ev.ready(hand,riichi);
            array<bool,4> locked{};
            for(int p=0;p<4;++p) locked[p]=st.pl[p].riichi;
            int after=ev.horizon==0?st.actor:st.seat;
            auto sources=mj::endWallRonSources(st.seat,after,ev.tailDraws,locked);
            cout<<json({{"cached",cached},{"raw",raw},{"horizon",ev.horizon},{"tail_draws",ev.tailDraws},
                {"self",st.seat},{"actor",st.actor},{"remaining",st.remaining},{"event",st.event},
                {"replacement_forecast",st.replacementForecast},{"distance_to_own_draw",ev.distance},
                {"other_wins",cx.otherWins.explain()},{"fold_continuation",ev.foldFloor(ev.horizon)},
                {"first_cycle_other_survival",ev.cycleInterval(ev.horizon,false).survival},
                {"first_cycle_riichi_other_survival",ev.cycleInterval(ev.horizon,true).survival},
                {"normal_source",sources.ordinary},{"last_source",sources.lastOrdinary},{"call_source",sources.extra},
                {"forecast",{{"value",changed.v},{"win",changed.win}}},
                {"without_end_wall_yaku",{{"value",ordinary.v},{"win",ordinary.win}}}}).dump()<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--ura-public-score") {
        string line;
        while(getline(cin,line)) {
            auto q=json::parse(line); auto input=q.at("query");
            mj::standing.load(input.value("match_context",json()));
            mj::State st; st.consume(input); mj::actionStarted=chrono::steady_clock::now();
            mj::prepareStanding(st); mj::Context cx(st); cx.assessThreats(); mj::Evaluator ev(cx);
            int out=mj::tile(q.at("discard").get<string>()),win=mj::norm(mj::tile(q.at("win").get<string>()));
            mj::Hand complete=st.hand; complete.add(mj::norm(out),-1); complete.add(win);
            if(mj::shanten(complete,cx.melds)!=-1) throw runtime_error("diagnostic hand does not win");
            ev.rootOut=out; json paths=json::array();
            for(const auto& path:q.at("paths")) {
                ev.futureDraws=mj::Hand{}; ev.futureDiscards=0;
                for(const auto& draw:path) { int t=mj::norm(mj::tile(draw.get<string>())); ev.futureDraws.add(t);if(cx.constraintModel)ev.futureWallDraws.add(t); ev.futureDiscards|=mj::U64(1)<<t; }
                auto score=ev.cachedWinScore(complete,win,true);
                auto inventory=ev.uraInventory(complete,win);
                auto key=mj::ExpectedBonuses::inventoryKey(inventory,ev.uraIndicatorCount());
                auto bonus=ev.bonusDistributions.at(key);
                paths.push_back({{"extra_draw_discards",path},{"bonus",bonus.explain()},
                    {"score_cache_size",ev.handScores.size()},{"inventory_key",key},
                    {"ron",score.ron},{"tsumo",score.tsumo},{"ron_utility",score.ronUtility},{"ron_by_payer",score.ronByPayer},{"tsumo_utility",score.tsumoUtility}});
            }
            mj::Scorer raw(complete,st,win,1,out); raw.expected=false; auto score=raw.run();
            cout<<json({{"paths",paths},{"raw_ron",score.ron},{"raw_tsumo",score.tsumo},
                       {"raw_ron_han",score.ronHan},{"raw_ron_fu",score.ronFu},
                       {"raw_tsumo_han",score.tsumoHan},{"raw_tsumo_fu",score.tsumoFu}}).dump()<<endl;
        }
        return 0;
    }
#ifdef LOCAL
    if(argc>2 && string(argv[1])=="--tune") {
        for(auto part:mj::split(argv[2],',')) {
            auto kv=mj::split(part,'=');if(kv.size()!=2)continue;double v=stod(kv[1]);auto &c=mj::cfg;
            if(kv[0]=="risk")c.risk=v;else if(kv[0]=="win")c.winBase=v;else if(kv[0]=="seat")c.winSeat=v;
            else if(kv[0]=="draw")c.drawRank=v;else if(kv[0]=="pressure")c.pressure=v;
            else if(kv[0]=="pon")c.pon=v;else if(kv[0]=="chi")c.chi=v;else if(kv[0]=="ron")c.ron=v;
            else if(kv[0]=="future")c.futureExtra=v;else if(kv[0]=="call")c.call=v!=0;else if(kv[0]=="tail")c.tail=v!=0;
        }
    }
    bool audit=argc>1 && string(argv[1])=="--audit";
    mj::debugPolicy=prt::runtime.analyze;
    if(argc>1 && string(argv[1])=="--shanten") {
        string line; while(getline(cin,line)) {
            mj::Hand h; auto w=mj::words(line); int m=0;
            for(auto s:w) { int t=mj::tile(s); if(t>=0) h.add(mj::norm(t)); else m=stoi(s); }
            cout<<mj::shanten(h,m)<<" "<<mj::standardShanten(h,m)<<endl;
        }
        return 0;
    }
    if(argc>1 && string(argv[1])=="--score") {
        string line; while(getline(cin,line)) {
            auto j=json::parse(line); mj::State st;
            st.seat=j.value("seat",0); st.round=j.value("round",0);
            for(auto s:j["hand"]) st.add(mj::tile(s.get<string>()),1);
            for(auto s:j.value("dora",vector<string>{})) ++st.dora[mj::doraNext(mj::tile(s))];
            for(auto m:j.value("melds",json::array())) {
                mj::Meld x; x.kind=m["kind"]; x.open=m.value("open",true);
                for(auto s:m["tiles"]) x.tiles.push_back(mj::tile(s.get<string>()));
                st.pl[st.seat].melds.push_back(x);
            }
            mj::Scorer scorer(st.hand,st,mj::norm(mj::tile(j["win"])),j.value("riichi",0),-1);
            scorer.expected=false; auto sc=scorer.run();
            cout<<json({{"ron",sc.ron},{"tsumo",sc.tsumo},{"ronHan",sc.ronHan},{"tsumoHan",sc.tsumoHan},{"ronFu",sc.ronFu},{"tsumoFu",sc.tsumoFu}}).dump()<<endl;
        }
        return 0;
    }
#endif
    mj::State state; string line;
    while(getline(cin,line)) {
        if(line.empty()) continue;
        try {
            mj::actionStarted=chrono::steady_clock::now();
            dt::begin(mj::actionStarted,mj::SEARCH_SECONDS,mj::debugPolicy);
            json input=json::parse(line);dt::input(input);
            if(input.contains("match_context")) mj::standing.load(input["match_context"]);
            state.consume(input);
            mj::bindCurrentOpponentConstraints(state,input);
            string answer=mj::act(state,input["requests"].back());
            json output={{"response",answer}};
#ifdef LOCAL
            if(audit) {
                vector<string> hand; for(int i=0;i<37;++i) for(int n=0;n<state.raw[i];++n) hand.push_back(mj::name(i));
                output["data"]={{"hand",hand},{"remaining",state.remaining},{"public",state.publicSeen},{"melds",state.meldCount()}};
            }
#endif
            const string telemetryReply=output.dump();
            mj::recordOpponentConstraintTelemetry(state);
            dt::emit(answer);
            cout<<telemetryReply<<'\n'<<flush;
        } catch(const exception& e) {
            cerr<<"exception: "<<e.what()<<'\n';
            dt::emit("PASS",e.what());
            cout<<"{\"response\":\"PASS\"}\n"<<flush;
        }
    }
    prt::runtime.shutdown();
    cerr<<json({{"type","parallel_shutdown"},{"runtime",prt::runtime.evidence()},{"active_jobs",prt::request.runningJobs.load()}}).dump()<<'\n';
}
