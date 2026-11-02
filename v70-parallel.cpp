#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <bits/stdc++.h>
#include <nlohmann/json.hpp>
using namespace std;
using json = nlohmann::json;

#ifndef TF_PACKAGED_RETENTION_POLICY_HPP
#define TF_PACKAGED_RETENTION_POLICY_HPP

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

#endif


#ifndef TF_PACKAGED_OPPONENT_CONSTRAINTS_HPP
#define TF_PACKAGED_OPPONENT_CONSTRAINTS_HPP

#include <array>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <nlohmann/json.hpp>

// Current hidden-inventory conditioning. No historical events are replayed here.
namespace ohc {
using json=nlohmann::json;using Counts=std::array<int,34>;using Clock=std::chrono::steady_clock;
struct BudgetExpired:std::runtime_error{BudgetExpired():std::runtime_error("constraint inference deadline") {}};
inline int tileIndex(const std::string& x){
    if(x.size()!=2)return -1;char s=x[0],r=x[1];
    if(s=='Z')return r>='1'&&r<='7'?27+r-'1':-1;
    int suit=s=='W'?0:s=='T'?1:s=='B'?2:-1;if(suit<0 || r<'0'||r>'9')return -1;
    return suit*9+(r=='0'?4:r-'1');
}
inline std::string tileName(int t){return std::string(1,t>=27?'Z':"WTB"[t/9])+char('1'+(t>=27?t-27:t%9));}
struct Bound{int seat=-1,count=0;};
struct Clause{int tile=-1;std::array<Bound,2> any;json provenance;};
struct Rule {
    int tile=0;std::array<unsigned char,125> allowed{};
    bool permits(const std::array<int,3>& x)const{return allowed[(x[0]*5+x[1])*5+x[2]]!=0;}
};
struct Metrics {
    std::atomic<uint64_t> builds{0},cacheHits{0},racedBuilds{0},cancelledBuilds{0},states{0},transitions{0},densityQueries{0},densityHits{0},nanos{0},policyContexts{0};
};
struct Work {
    Metrics& metrics;std::function<bool()> stop;Clock::time_point begin=Clock::now();uint64_t states=0,transitions=0;bool finished=false;
    Work(Metrics& m,std::function<bool()> s):metrics(m),stop(std::move(s)){++metrics.builds;}
    ~Work(){metrics.states+=states;metrics.transitions+=transitions;metrics.nanos+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count());if(!finished)++metrics.cancelledBuilds;}
    void check(){if(stop && stop())throw BudgetExpired{};}
};
struct Combinations {
    std::array<std::array<double,15>,137> c{};
    Combinations(){for(int n=0;n<=136;++n){c[n][0]=1;for(int k=1;k<=std::min(14,n);++k)c[n][k]=(n?c[n-1][k]:0)+c[n-1][k-1];}}
    double choose(int n,int k)const{
        if(n<0 || k<0 || k>n || n>136)return 0;k=std::min(k,n-k);
        if(k<=14)return c[n][k];double out=1;for(int i=1;i<=k;++i)out*=double(n-i+1)/i;return out;
    }
    double allocations(int n,const std::array<int,3>& h)const{return choose(n,h[0])*choose(n-h[0],h[1])*choose(n-h[0]-h[1],h[2]);}
    double hypergeom(int n,int copies,int size,int k)const{
        double denominator=choose(n,size);return denominator>0?choose(copies,k)*choose(n-copies,size-k)/denominator:0;
    }
};
struct Pattern{std::array<int,3> x{};int wall=0;double ways=1;};
struct Factor{int tile=0,copies=0;std::vector<Pattern> patterns;};
struct Core {
    bool possible=false;double partition=0,unconditional=0;int unknown=0,wall=0,freeCopies=0,freeWallMax=0;
    std::array<bool,34> explicitTile{};
    std::array<double,34> wallMean{};std::array<int,34> wallMax{};
    std::array<std::array<double,34>,3> heldMean{};
    std::array<std::array<std::array<double,5>,34>,3> heldCount{};
    std::array<std::array<std::array<double,34>,34>,3> cross{};
    std::array<double,3> freeHeld{},freeHeldSecond{};double freeWallMean=0;
    std::array<std::array<double,15>,3> freeSizeMass{};
    double nextWallMean(int t,int physicalCopies)const{return explicitTile[t]?wallMean[t]:freeCopies?physicalCopies*freeWallMean/freeCopies:0;}
    int nextWallMax(int t,int physicalCopies)const{return explicitTile[t]?wallMax[t]:std::min(physicalCopies,freeWallMax);}
    double holdingMean(int p,int t,int physicalCopies)const{return explicitTile[t]?heldMean[p][t]:freeCopies?physicalCopies*freeHeld[p]/freeCopies:0;}
};
struct Distribution {
    bool possible=false;int remainingUnknown=0;
    std::array<double,34> wall{};
    std::array<std::array<double,34>,4> release{};
};

// Local tile assignments have at most C(7,3)=35 possibilities. The dynamic
// program is over three hand capacities, not over OR-clause truth choices.
inline std::shared_ptr<Core> solve(const Counts& inventory,const std::array<int,3>& capacity,
        const std::vector<Rule>& rules,const Combinations& comb,Metrics& metrics,const std::function<bool()>& stop,
        bool fullStats=false,int fixedSeat=-1,const Counts& fixedHand=Counts{}){
    Work work(metrics,stop);work.check();auto out=std::make_shared<Core>();
    out->unknown=std::accumulate(inventory.begin(),inventory.end(),0);
    out->wall=out->unknown-std::accumulate(capacity.begin(),capacity.end(),0);
    if(out->wall<0){work.finished=true;return out;}
    out->unconditional=comb.allocations(out->unknown,capacity);
    std::vector<Factor> factors;int constrainedCopies=0;
    for(const auto& rule:rules){
        int t=rule.tile,u=inventory[t];Factor f;f.tile=t;f.copies=u;size_t physical=0;
        for(int a=0;a<=std::min(u,capacity[0]);++a)for(int b=0;b<=std::min(u-a,capacity[1]);++b)for(int c=0;c<=std::min(u-a-b,capacity[2]);++c){
            int w=u-a-b-c;if(w>out->wall)continue;++physical;std::array<int,3>x{{a,b,c}},original=x;
            if(fixedSeat>=0)original[fixedSeat]=fixedHand[t];
            if(!rule.permits(original))continue;
            f.patterns.push_back({x,w,comb.choose(u,a)*comb.choose(u-a,b)*comb.choose(u-a-b,c)});
        }
        if(f.patterns.empty()){work.finished=true;return out;}
        if(f.patterns.size()!=physical){constrainedCopies+=u;out->explicitTile[t]=true;factors.push_back(std::move(f));}
    }
    out->freeCopies=out->unknown-constrainedCopies;
    const int d1=capacity[1]+1,d2=capacity[2]+1,states=(capacity[0]+1)*d1*d2,m=int(factors.size());
    auto index=[&](int a,int b,int c){return(a*d1+b)*d2+c;};
    std::vector<std::array<int,3>> coordinates(states);
    for(int a=0;a<=capacity[0];++a)for(int b=0;b<=capacity[1];++b)for(int c=0;c<=capacity[2];++c)coordinates[index(a,b,c)]={{a,b,c}};
    std::vector<int> prefix(m+1);for(int k=0;k<m;++k)prefix[k+1]=prefix[k]+factors[k].copies;
    std::vector<std::vector<double>> backward(m+1,std::vector<double>(states));
    std::array<std::vector<std::vector<double>>,3> backwardFree;
    if(fullStats)for(auto& a:backwardFree)a.assign(m+1,std::vector<double>(states));
    for(int s=0;s<states;++s){
        auto x=coordinates[s];int wallUsed=prefix[m]-x[0]-x[1]-x[2];if(wallUsed<0||wallUsed>out->wall)continue;
        std::array<int,3> left{{capacity[0]-x[0],capacity[1]-x[1],capacity[2]-x[2]}};
        double ways=comb.allocations(out->freeCopies,left);backward[m][s]=ways;
        if(fullStats)for(int p=0;p<3;++p)backwardFree[p][m][s]=ways*left[p];
    }
    auto nextIndex=[&](const std::array<int,3>& x,const Pattern& pat,int after)->int{
        std::array<int,3> y{{x[0]+pat.x[0],x[1]+pat.x[1],x[2]+pat.x[2]}};
        if(y[0]>capacity[0]||y[1]>capacity[1]||y[2]>capacity[2])return-1;
        int used=after-y[0]-y[1]-y[2];if(used<0||used>out->wall)return-1;return index(y[0],y[1],y[2]);
    };
    for(int k=m-1;k>=0;--k){work.check();
        for(int s=0;s<states;++s){if((s&255)==0)work.check();auto x=coordinates[s];int wallUsed=prefix[k]-x[0]-x[1]-x[2];
            if(wallUsed<0||wallUsed>out->wall)continue;++work.states;
            for(const auto& pat:factors[k].patterns){++work.transitions;int n=nextIndex(x,pat,prefix[k+1]);if(n<0)continue;
                backward[k][s]+=pat.ways*backward[k+1][n];
                if(fullStats)for(int p=0;p<3;++p)backwardFree[p][k][s]+=pat.ways*backwardFree[p][k+1][n];
            }
        }
    }
    double z=backward[0][0];out->partition=z;if(!(z>0)){work.finished=true;return out;}out->possible=true;
    std::vector<double> forward(states);forward[0]=1;
    struct Moment{int tile;std::array<std::vector<double>,3> value;};std::vector<Moment> active;
    std::array<std::array<double,34>,3> constrainedFree{};
    for(int k=0;k<m;++k){work.check();int tile=factors[k].tile;
        active.erase(std::remove_if(active.begin(),active.end(),[&](const Moment& a){return a.tile/9!=tile/9 || tile>=27 || tile-a.tile>2;}),active.end());
        std::vector<double> next(states);std::vector<Moment> nextMoments;
        if(fullStats){for(const auto& a:active){Moment n;n.tile=a.tile;for(auto& q:n.value)q.assign(states,0);nextMoments.push_back(std::move(n));}
            Moment n;n.tile=tile;for(auto& q:n.value)q.assign(states,0);nextMoments.push_back(std::move(n));}
        for(int s=0;s<states;++s)if(forward[s]>0){if((s&255)==0)work.check();++work.states;auto x=coordinates[s];
            for(const auto& pat:factors[k].patterns){++work.transitions;int n=nextIndex(x,pat,prefix[k+1]);if(n<0||backward[k+1][n]==0)continue;
                double path=forward[s]*pat.ways,mass=path*backward[k+1][n]/z;next[n]+=path;
                out->wallMean[tile]+=mass*pat.wall;out->wallMax[tile]=std::max(out->wallMax[tile],pat.wall);
                for(int p=0;p<3;++p){out->heldMean[p][tile]+=mass*pat.x[p];
                    if(fullStats){out->heldCount[p][tile][pat.x[p]]+=mass;
                        constrainedFree[p][tile]+=path*pat.x[p]*backwardFree[p][k+1][n]/z;
                        nextMoments.back().value[p][n]+=path*pat.x[p];}}
                if(fullStats)for(size_t a=0;a<active.size();++a)for(int p=0;p<3;++p){
                    double weighted=active[a].value[p][s]*pat.ways;nextMoments[a].value[p][n]+=weighted;
                    out->cross[p][active[a].tile][tile]+=weighted*pat.x[p]*backward[k+1][n]/z;
                }
            }
        }
        forward=std::move(next);active=std::move(nextMoments);
    }
    for(int s=0;s<states;++s)if(forward[s]>0&&backward[m][s]>0){++work.states;auto x=coordinates[s];double mass=forward[s]*backward[m][s]/z;
        int freeWall=out->freeCopies;for(int p=0;p<3;++p){int h=capacity[p]-x[p];freeWall-=h;out->freeHeld[p]+=mass*h;
            out->freeHeldSecond[p]+=mass*h*(h-1);out->freeSizeMass[p][h]+=mass;}
        out->freeWallMean+=mass*freeWall;out->freeWallMax=std::max(out->freeWallMax,freeWall);
    }
    if(fullStats){
        for(int p=0;p<3;++p)for(int t=0;t<34;++t)if(!out->explicitTile[t]){
            out->heldMean[p][t]=out->holdingMean(p,t,inventory[t]);
            for(int h=0;h<=capacity[p];++h)if(out->freeSizeMass[p][h]>0)
                for(int n=0;n<=std::min(4,inventory[t]);++n)out->heldCount[p][t][n]+=out->freeSizeMass[p][h]*comb.hypergeom(out->freeCopies,inventory[t],h,n);
        }
        for(int p=0;p<3;++p)for(int a=0;a<27;++a)for(int b=a+1;b<=std::min(a+2,a/9*9+8);++b){
            if(out->explicitTile[a]&&out->explicitTile[b]){}
            else if(out->explicitTile[a])out->cross[p][a][b]=out->freeCopies?inventory[b]*constrainedFree[p][a]/out->freeCopies:0;
            else if(out->explicitTile[b])out->cross[p][a][b]=out->freeCopies?inventory[a]*constrainedFree[p][b]/out->freeCopies:0;
            else out->cross[p][a][b]=out->freeCopies>1?double(inventory[a])*inventory[b]*out->freeHeldSecond[p]/(out->freeCopies*(out->freeCopies-1)):0;
            out->cross[p][b][a]=out->cross[p][a][b];
        }
    }
    work.finished=true;return out;
}

struct Model {
    Counts inventory{};std::array<int,3> capacity{},seat{};std::array<int,4> slot{{-1,-1,-1,-1}};
    std::vector<Rule> rules;std::array<bool,34> constrained{};std::string signature;
    Combinations comb;Metrics metrics;std::function<bool()> stop;std::shared_ptr<Core> root;
    std::mutex cacheMutex;std::map<std::string,std::shared_ptr<const Core>> drawCache;std::map<std::string,double> densityCache;
    std::string key(const Counts& wallDraws)const{
        std::string k;int other=0;for(int t=0;t<34;++t)if(constrained[t])k.push_back(char(wallDraws[t]));else other+=wallDraws[t];
        k.push_back(char(other));return k;
    }
    std::shared_ptr<const Core> afterWallDraws(const Counts& draws){
        auto k=key(draws);{std::lock_guard<std::mutex> lock(cacheMutex);auto f=drawCache.find(k);if(f!=drawCache.end()){++metrics.cacheHits;return f->second;}}
        Counts remaining=inventory;int other=0;
        for(int t=0;t<34;++t){if(draws[t]<0||draws[t]>inventory[t])return {};if(constrained[t])remaining[t]-=draws[t];else other+=draws[t];}
        // Unconstrained tile identities affect their individual marginals only
        // through their remaining copy counts; the capacity DP needs their sum.
        for(int t=0;t<34&&other;++t)if(!constrained[t]){int take=std::min(other,remaining[t]);remaining[t]-=take;other-=take;}
        if(other)return {};
        auto result=solve(remaining,capacity,rules,comb,metrics,stop);
        std::lock_guard<std::mutex> lock(cacheMutex);auto inserted=drawCache.emplace(k,result);if(!inserted.second)++metrics.racedBuilds;return inserted.first->second;
    }
    double exactHandDensity(int absoluteSeat,const Counts& hand){
        ++metrics.densityQueries;int p=absoluteSeat>=0&&absoluteSeat<4?slot[absoluteSeat]:-1;if(p<0)return 0;
        if(std::accumulate(hand.begin(),hand.end(),0)!=capacity[p])return 0;
        std::string k(1,char(p));for(int t=0;t<34;++t){if(hand[t]<0||hand[t]>inventory[t])return 0;if(constrained[t])k.push_back(char(hand[t]));}
        {std::lock_guard<std::mutex> lock(cacheMutex);auto f=densityCache.find(k);if(f!=densityCache.end()){++metrics.densityHits;return f->second;}}
        Counts remaining=inventory;for(int t=0;t<34;++t)remaining[t]-=hand[t];auto caps=capacity;caps[p]=0;
        auto fixed=solve(remaining,caps,rules,comb,metrics,stop,false,p,hand);
        double value=fixed->possible?(fixed->partition/fixed->unconditional)/(root->partition/root->unconditional):0;
        std::lock_guard<std::mutex> lock(cacheMutex);densityCache.emplace(k,value);return value;
    }
    double countRatio(int absoluteSeat,int tile,int first,int last)const{
        int p=slot[absoluteSeat];double prior=0,posterior=0;
        for(int k=first;k<=last;++k){prior+=comb.hypergeom(root->unknown,inventory[tile],capacity[p],k);posterior+=root->heldCount[p][tile][k];}
        return prior>0?posterior/prior:0;
    }
    double neighborRatio(int absoluteSeat,int a,int b)const{
        if(a<0||b<0||a>=27||b>=27||a/9!=b/9||a==b||std::abs(a-b)>2)throw std::logic_error("unsupported neighbor moment request");
        int p=slot[absoluteSeat];double n=root->unknown;
        double prior=n>1?double(capacity[p])*(capacity[p]-1)*inventory[a]*inventory[b]/(n*(n-1)):0;
        return prior>0?root->cross[p][a][b]/prior:0;
    }
    bool exchangeable(int a,int b)const{
        if(inventory[a]!=inventory[b])return false;const Rule* ra=nullptr;const Rule* rb=nullptr;
        for(const auto& r:rules){if(r.tile==a)ra=&r;if(r.tile==b)rb=&r;}
        return (!ra&&!rb)||(ra&&rb&&ra->allowed==rb->allowed);
    }
    Distribution distribution(const Counts& allAcquired,const Counts& wallDraws,const std::array<bool,4>& locked){
        Distribution out;auto core=afterWallDraws(wallDraws);
        if(!core||!core->possible)return out;out.possible=true;
        std::array<std::array<double,34>,3> held{};std::array<double,3> heldTotal{};double wallTotal=0;
        for(int t=0;t<34;++t){int before=inventory[t]-wallDraws[t],left=inventory[t]-allAcquired[t];
            if(before<0||left<0)throw std::logic_error("constraint inventory overdraw");out.remainingUnknown+=left;
            if(!before||!left)continue;double fraction=double(left)/before;
            out.wall[t]=std::min(double(left),core->nextWallMean(t,before)*fraction);wallTotal+=out.wall[t];
            for(int p=0;p<3;++p){held[p][t]=std::min(double(left),core->holdingMean(p,t,before)*fraction);heldTotal[p]+=held[p][t];}
        }
        if(wallTotal>0)for(double& x:out.wall)x/=wallTotal;
        for(int p=0;p<3;++p){
            if(locked[seat[p]])out.release[seat[p]]=out.wall;
            else{double total=heldTotal[p]+(wallTotal>0?1.:0.);
                if(total>0)for(int t=0;t<34;++t)out.release[seat[p]][t]=(held[p][t]+out.wall[t])/total;}
        }
        return out;
    }
    std::array<double,34> wallProbabilities(const Counts& allAcquired,const Counts& wallDraws){
        std::array<double,34> result{};auto core=afterWallDraws(wallDraws);if(!core||!core->possible||core->wall<=0)return result;
        double total=0;for(int t=0;t<34;++t){int remaining=inventory[t]-wallDraws[t],physical=inventory[t]-allAcquired[t];
            if(remaining<=0||physical<=0)continue;double mean=core->nextWallMean(t,remaining);
            // A hypothetical call is not a wall draw. Its unknown origin is
            // handled only by a labelled proportional depletion approximation.
            mean*=double(physical)/remaining;result[t]=std::min(double(physical),mean);total+=result[t];}
        if(total>0)for(double& x:result)x/=total;return result;
    }
    double releaseRatio(int absoluteSeat,int tile,bool locked,const Counts& allAcquired,const Counts& wallDraws,
            const std::array<double,34>& wallProbability){
        int p=slot[absoluteSeat],physical=inventory[tile]-allAcquired[tile],n=0;
        for(int t=0;t<34;++t)n+=std::max(0,inventory[t]-allAcquired[t]);
        if(p<0||physical<=0||n<=0)return 0;double prior=double(physical)/n,probability=wallProbability[tile];
        if(!locked){auto core=afterWallDraws(wallDraws);if(!core||!core->possible)return 0;
            int before=inventory[tile]-wallDraws[tile];double held=core->holdingMean(p,tile,before);
            if(before>0)held*=double(physical)/before;
            probability=(held+probability)/(capacity[p]+1);}
        return probability/prior;
    }
};

struct Snapshot {
    std::string status="absent";json issues=json::array(),meta=json::object();std::vector<Clause> clauses;
    std::shared_ptr<Model> model;std::once_flag initialized;size_t inputCount=0;int self=0;
    std::function<bool()> stop;double rootSeconds=0;bool used=false;
    static std::shared_ptr<Snapshot> parse(const json& input,int self,const std::function<bool()>& stop){
        auto out=std::make_shared<Snapshot>();out->self=self;out->stop=stop;
        if(self<0||self>3){out->status="invalid";out->issues.push_back({{"code","invalid_engine_self_seat"}});return out;}
        if(!input.contains("opponent_hand_constraints"))return out;
        const auto& q=input["opponent_hand_constraints"];out->status="invalid";
        auto issue=[&](const char* code,int i=-1){out->issues.push_back({{"code",code},{"entry",i<0?json(nullptr):json(i)}});};
        if(!q.is_object()){issue("snapshot_not_object");return out;}
        for(const char* k:{"round","as_of_frame_id"})if(q.contains(k))out->meta[k]=q[k];
        if(!q.contains("schema")||!q["schema"].is_number_integer()||q["schema"]!=2)issue("unsupported_schema");
        if(!q.contains("time_scope")||q["time_scope"]!="current")issue("unsupported_time_scope");
        if(!q.contains("constraints")||!q["constraints"].is_array()){issue("constraints_not_array");return out;}
        out->inputCount=q["constraints"].size();
        for(size_t i=0;i<out->inputCount;++i){const auto& row=q["constraints"][i];bool valid=true;Clause c;
            if(!row.is_object()){issue("entry_not_object",int(i));continue;}
            if(!row.contains("tile")||!row["tile"].is_string()||(c.tile=tileIndex(row["tile"].get<std::string>()))<0){issue("invalid_tile_encoding",int(i));valid=false;}
            if(!row.contains("any_of")||!row["any_of"].is_array()||row["any_of"].size()!=2){issue("any_of_requires_two_alternatives",int(i));continue;}
            for(int j=0;j<2;++j){const auto& a=row["any_of"][j];
                if(!a.is_object()||!a.contains("seat")||!a["seat"].is_number_integer()||a["seat"]<0||a["seat"]>3){issue("invalid_absolute_seat",int(i));valid=false;continue;}
                c.any[j].seat=a["seat"].get<int>();if(c.any[j].seat==self){issue("self_is_not_an_opponent",int(i));valid=false;}
                if(!a.contains("minimum_count")||!a["minimum_count"].is_number_integer()||a["minimum_count"]<0||a["minimum_count"]>4){issue("invalid_minimum_count",int(i));valid=false;continue;}
                c.any[j].count=a["minimum_count"].get<int>();
            }
            if(valid){c.provenance=row;out->clauses.push_back(std::move(c));}
        }
        if(!out->issues.empty()){out->clauses.clear();return out;}
        out->status=out->inputCount?"parsed_not_evaluated":"empty";return out;
    }
    std::shared_ptr<Model> resolve(const Counts& inventory,const std::array<int,4>& capacities,bool inventoryConsistent){
        if(status=="absent"||status=="empty"||status=="invalid")return {};
        std::call_once(initialized,[&]{auto start=Clock::now();
            auto candidate=std::make_shared<Model>();candidate->inventory=inventory;candidate->stop=stop;
            int n=0;for(int p=0;p<4;++p)if(p!=self){candidate->seat[n]=p;candidate->slot[p]=n;candidate->capacity[n++]=capacities[p];}
            int total=std::accumulate(inventory.begin(),inventory.end(),0),h=std::accumulate(candidate->capacity.begin(),candidate->capacity.end(),0),wall=total-h;
            bool sane=inventoryConsistent&&wall>=0;for(int p:candidate->capacity)sane&=p>=0&&p<=14;for(int u:inventory)sane&=u>=0&&u<=4;
            if(!sane){status="unsupported_public_inventory";issues.push_back({{"code","inconsistent_current_public_inventory_or_hand_sizes"}});return;}
            std::array<std::vector<const Clause*>,34> byTile;for(const auto& c:clauses)byTile[c.tile].push_back(&c);
            for(int t=0;t<34;++t)if(!byTile[t].empty()){
                Rule rule;rule.tile=t;int physical=0,accepted=0;
                for(int a=0;a<=std::min(inventory[t],candidate->capacity[0]);++a)for(int b=0;b<=std::min(inventory[t]-a,candidate->capacity[1]);++b)for(int c=0;c<=std::min(inventory[t]-a-b,candidate->capacity[2]);++c){
                    if(inventory[t]-a-b-c>wall)continue;++physical;std::array<int,3>x{{a,b,c}};bool valid=true;
                    for(const auto* entry:byTile[t]){bool any=false;for(const auto& side:entry->any)any|=x[candidate->slot[side.seat]]>=side.count;valid&=any;}
                    if(valid){rule.allowed[(a*5+b)*5+c]=1;++accepted;}
                }
                if(!accepted){status="unsatisfiable";issues.push_back({{"code","tile_factor_has_no_physical_assignment"},{"tile",tileName(t)}});return;}
                if(accepted!=physical){candidate->rules.push_back(rule);candidate->constrained[t]=true;}
            }
            if(candidate->rules.empty()){status="redundant_no_effect";return;}
            candidate->signature=std::to_string(self)+":";for(int x:inventory)candidate->signature+=char('0'+x);
            for(int x:candidate->capacity)candidate->signature+=":"+std::to_string(x);
            for(const auto& rule:candidate->rules){candidate->signature+=":"+std::to_string(rule.tile)+":";for(unsigned char b:rule.allowed)candidate->signature+=char('0'+b);}
            model=candidate; // retain counters even if inference is interrupted
            try{
                candidate->root=solve(inventory,candidate->capacity,candidate->rules,candidate->comb,candidate->metrics,stop,true);
                if(!candidate->root->possible){status="unsatisfiable";issues.push_back({{"code","joint_AND_exceeds_physical_hand_capacities"}});}
                else{Counts zero{};candidate->drawCache.emplace(candidate->key(zero),candidate->root);status="applied";used=true;}
            }catch(const BudgetExpired&){status="inference_deadline";issues.push_back({{"code","conditioning_incomplete_no_partial_model_applied"}});}
            rootSeconds=std::chrono::duration<double>(Clock::now()-start).count();
        });
        return used?model:std::shared_ptr<Model>{};
    }
    json summary()const{
        json out={{"schema",2},{"time_scope","current"},{"status",status},{"applied",used},{"input_entries",inputCount},
            {"snapshot_replaced",true},{"history_decrements_applied",0},{"metadata",meta},{"issues",issues},
            {"round_semantics","opaque_upstream_provenance_not_round_wind"},{"root_inference_seconds",rootSeconds}};
        if(status=="invalid"||status=="unsatisfiable"||status=="unsupported_public_inventory"||status=="inference_deadline")out["fallback"]="explicit_unconditioned_parent_model";
        if(model){out["canonical_tile_factors"]=model->rules.size();out["inventory_total"]=std::accumulate(model->inventory.begin(),model->inventory.end(),0);
            out["policy_contexts"]=model->metrics.policyContexts.load();out["consumed_by_policy"]=used&&model->metrics.policyContexts.load()>0;
            out["dp_builds"]=model->metrics.builds.load();out["dp_states"]=model->metrics.states.load();out["dp_transitions"]=model->metrics.transitions.load();
            out["cache_hits"]=model->metrics.cacheHits.load();out["raced_builds"]=model->metrics.racedBuilds.load();out["cancelled_builds"]=model->metrics.cancelledBuilds.load();
            out["exact_hand_density_queries"]=model->metrics.densityQueries.load();out["exact_hand_density_cache_hits"]=model->metrics.densityHits.load();
            out["inference_thread_seconds"]=model->metrics.nanos.load()*1e-9;
            if(used){out["residual_wall_pool"]=model->root->wall;out["constraint_prior_probability"]=model->root->partition/model->root->unconditional;}}
        return out;
    }
    json analysis()const{
        json out=summary();out["constraints_original"]=json::array();for(const auto& c:clauses)out["constraints_original"].push_back(c.provenance);
        if(!used)return out;auto& m=*model;auto& c=*m.root;json rows=json::array();
        for(int t=0;t<34;++t){json held=json::array();for(int p=0;p<3;++p)held.push_back({{"seat",m.seat[p]},{"concealed_capacity",m.capacity[p]},
            {"mean",c.heldMean[p][t]},{"count_distribution",c.heldCount[p][t]}});
            rows.push_back({{"tile",tileName(t)},{"unknown_copies",m.inventory[t]},{"expected_wall_copies",c.nextWallMean(t,m.inventory[t])},
                {"maximum_wall_copies",c.nextWallMax(t,m.inventory[t])},{"next_wall_probability",c.wall>0?json(c.nextWallMean(t,m.inventory[t])/c.wall):json(nullptr)},
                {"opponent_holdings",held}});}
        out["posterior_inventory"]=rows;return out;
    }
};
}

#endif


#ifndef TF_PACKAGED_WIN_DIAGNOSTICS_HPP
#define TF_PACKAGED_WIN_DIAGNOSTICS_HPP

// Output-only observations of existing v64 estimates and already selected overrides.
// No evaluator, probabilities model, action eligibility or score selection lives here.
namespace winout {
inline json value(double probability,bool available){return available?json(probability):json(nullptr);}
inline json afterRisk(double probability,bool available,double survival){return value(survival*probability,available);}
struct CallObservation {
    double probability=0;bool available=false;
    void evaluated(double p){probability=p;available=true;}
    void folded(){probability=0;available=true;}
    void nagashi(){available=false;}
    json output()const{return value(probability,available);}
};
inline json discardRow(const std::string& tile,double score,double off,bool riichi,
                       double survival,double loss,double probability,bool available,bool selected){
    return {{"tile",tile},{"score",score},{"off",off},{"riichi",riichi},
        {"current_p_ron",1-survival},{"current_loss",loss},
        {"win",value(probability,available)},{"win_after_current_risk",afterRisk(probability,available,survival)},
        {"win_status",available?"estimated_policy_current_hand_conditional_on_current_discard_passing":"nagashi_policy_win_not_computed"},
        {"selected",selected}};
}
inline json budgetFallback(const std::string& response,const json& estimates,double seconds,double budget){
    return {{"budget_fallback",true},{"response",response},{"estimates",estimates},
        {"estimate_status",estimates.is_null()?"not_computed":"last_complete_pass"},
        {"seconds",seconds},{"search_budget_seconds",budget}};
}
}

#endif


#ifndef TF_PACKAGED_PARALLEL_RUNTIME_HPP
#define TF_PACKAGED_PARALLEL_RUNTIME_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cpuid.h>

namespace prt {
using Clock=std::chrono::steady_clock;
inline double seconds(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double>(b-a).count();}
inline uint64_t filetime(FILETIME x){return (uint64_t(x.dwHighDateTime)<<32)|x.dwLowDateTime;}
inline double threadCpu(){FILETIME a,b,k,u;if(!GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u))return -1;return (filetime(k)+filetime(u))*1e-7;}
inline std::string hexMask(uint64_t x){std::ostringstream s;s<<"0x"<<std::hex<<x;return s.str();}
struct RuntimeError:std::runtime_error {
    DWORD code;std::string operation;
    RuntimeError(const std::string& op,DWORD error):std::runtime_error(op+" failed, Win32="+std::to_string(error)),code(error),operation(op){}
};
inline void requireWin(bool ok,const char* operation){if(!ok)throw RuntimeError(operation,GetLastError());}
struct Cpu {unsigned core=0,group=0,logical=0,efficiency=0;ULONG setId=0;bool allocatedElsewhere=false;};
struct Core {unsigned index=0,group=0,efficiency=0;uint64_t mask=0;bool smt=false;};
using GetSetsFn=BOOL(WINAPI*)(void*,ULONG,PULONG,HANDLE,ULONG);
using SetProcessSetsFn=BOOL(WINAPI*)(HANDLE,const ULONG*,ULONG);
using GetProcessSetsFn=BOOL(WINAPI*)(HANDLE,PULONG,ULONG,PULONG);
using SetThreadSetsFn=BOOL(WINAPI*)(HANDLE,const ULONG*,ULONG);
using GetThreadSetsFn=BOOL(WINAPI*)(HANDLE,PULONG,ULONG,PULONG);
struct Api {
    GetSetsFn getSets=nullptr;SetProcessSetsFn setProcessSets=nullptr;GetProcessSetsFn getProcessSets=nullptr;
    SetThreadSetsFn setThreadSets=nullptr;GetThreadSetsFn getThreadSets=nullptr;
    void load(){auto k=GetModuleHandleA("kernel32.dll");
        getSets=reinterpret_cast<GetSetsFn>(GetProcAddress(k,"GetSystemCpuSetInformation"));
        setProcessSets=reinterpret_cast<SetProcessSetsFn>(GetProcAddress(k,"SetProcessDefaultCpuSets"));
        getProcessSets=reinterpret_cast<GetProcessSetsFn>(GetProcAddress(k,"GetProcessDefaultCpuSets"));
        setThreadSets=reinterpret_cast<SetThreadSetsFn>(GetProcAddress(k,"SetThreadSelectedCpuSets"));
        getThreadSets=reinterpret_cast<GetThreadSetsFn>(GetProcAddress(k,"GetThreadSelectedCpuSets"));
        if(!getSets||!setProcessSets||!getProcessSets||!setThreadSets||!getThreadSets)throw RuntimeError("required CPU-set API unavailable",ERROR_PROC_NOT_FOUND);
    }
};
struct RequestControl {
    bool active=false;uint64_t sequence=0;Clock::time_point start{},deadline{};
    std::atomic<bool> cancelled{false},timeStop{false};std::atomic<uint64_t> chargedNodes{0};
    std::mutex unionMutex;int unionActive=0,unionPeak=0;Clock::time_point unionStart{};double unionSeconds=0;
    double dispatchSeconds=0,joinWaitSeconds=0;unsigned batches=0,tasksScheduled=0,tasksCompleted=0,incompleteBatches=0;
    std::atomic<unsigned> runningJobs{0};double mainCpuStart=0;
    void begin(uint64_t seq,Clock::time_point started,double budget){
        active=true;sequence=seq;start=started;deadline=start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(budget));
        cancelled=false;timeStop=false;chargedNodes=0;unionActive=unionPeak=0;unionSeconds=0;
        dispatchSeconds=joinWaitSeconds=0;batches=tasksScheduled=tasksCompleted=incompleteBatches=0;runningJobs=0;mainCpuStart=threadCpu();
    }
    bool stop(){if(!active)return false;if(Clock::now()>=deadline){timeStop=true;cancelled=true;}return cancelled.load(std::memory_order_relaxed);}
    // Observation only: node counts never cancel a request. The common wall deadline is unchanged.
    void charge(){if(active)chargedNodes.fetch_add(1,std::memory_order_relaxed);}
    void searchEnter(){if(!active)return;std::lock_guard<std::mutex> l(unionMutex);if(unionActive++==0)unionStart=Clock::now();unionPeak=std::max(unionPeak,unionActive);}
    void searchLeave(){if(!active)return;std::lock_guard<std::mutex> l(unionMutex);if(--unionActive==0)unionSeconds+=seconds(unionStart,Clock::now());}
};
inline RequestControl request;
struct Worker {
    std::thread thread;DWORD threadId=0;json binding;bool ready=false,ok=false;std::string error,requestError;
    uint64_t requestSeen=0;double cpuStart=0,cpuSeconds=0,setupSeconds=0;unsigned batches=0;
};
class Runtime {
    Api api;std::vector<Core> cores;std::vector<Cpu> cpuSets;std::vector<Cpu> selected;
    std::vector<std::unique_ptr<Worker>> workers;
    std::mutex mutex;std::condition_variable wake,finished;uint64_t generation=0;int pending=0;
    bool quitting=false,started=false;std::function<void(size_t)> job;
    uint64_t originalMask=0,processMask=0,systemMask=0;DWORD processClass=0;
    json topology;double startupSeconds=0;unsigned joinedThreads=0;
    json bind(size_t id){const auto& cpu=selected.at(id);
        requireWin(api.setThreadSets(GetCurrentThread(),&cpu.setId,1),"SetThreadSelectedCpuSets");
        GROUP_AFFINITY desired{},previous{};desired.Group=WORD(cpu.group);desired.Mask=KAFFINITY(1)<<cpu.logical;
        requireWin(SetThreadGroupAffinity(GetCurrentThread(),&desired,&previous),"SetThreadGroupAffinity");
        requireWin(SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL),"SetThreadPriority(normal relative to HIGH class)");
        GROUP_AFFINITY actual{};requireWin(GetThreadGroupAffinity(GetCurrentThread(),&actual),"GetThreadGroupAffinity");
        ULONG count=0;api.getThreadSets(GetCurrentThread(),nullptr,0,&count);std::vector<ULONG> sets(count);
        requireWin(api.getThreadSets(GetCurrentThread(),sets.data(),count,&count),"GetThreadSelectedCpuSets");
        PROCESSOR_NUMBER current{};GetCurrentProcessorNumberEx(&current);
        bool exact=actual.Group==cpu.group && actual.Mask==(KAFFINITY(1)<<cpu.logical) && sets.size()==1 && sets[0]==cpu.setId;
        if(!exact)throw RuntimeError("thread affinity verification mismatch",ERROR_INVALID_DATA);
        return {{"worker",id},{"physical_core",cpu.core},{"efficiency_class",cpu.efficiency},{"logical_cpu",cpu.logical},{"group",cpu.group},
            {"thread_id",GetCurrentThreadId()},{"requested_mask",hexMask(uint64_t(desired.Mask))},{"actual_mask",hexMask(uint64_t(actual.Mask))},
            {"actual_group",actual.Group},{"cpu_set_ids",sets},{"observed_cpu",current.Number},{"observed_group",current.Group},
            {"thread_priority_relative",GetThreadPriority(GetCurrentThread())},{"process_priority_class",GetPriorityClass(GetCurrentProcess())},{"verified",exact}};
    }
    void run(size_t id,const std::function<void(size_t)>& f){
        auto& w=*workers[id];if(w.requestSeen!=request.sequence){w.requestSeen=request.sequence;w.cpuStart=threadCpu();w.setupSeconds=0;w.batches=0;w.requestError.clear();}
        request.runningJobs.fetch_add(1);try{f(id);}catch(const std::exception& e){w.requestError=e.what();request.cancelled=true;}catch(...){w.requestError="unclassified worker exception";request.cancelled=true;}
        request.runningJobs.fetch_sub(1);++w.batches;
        double cpu=threadCpu();w.cpuSeconds=cpu>=0 && w.cpuStart>=0?cpu-w.cpuStart:-1;
        GROUP_AFFINITY actual{};bool ok=GetThreadGroupAffinity(GetCurrentThread(),&actual);
        w.ok=ok && actual.Group==selected[id].group && actual.Mask==(KAFFINITY(1)<<selected[id].logical);
        if(!w.ok){w.error="affinity changed during request";request.cancelled=true;}
    }
public:
    bool runtimeInfoOnly=false,analyze=false;unsigned requestedWorkers=0;
    size_t count()const{return selected.size();}
    void configure(int argc,char** argv){
        for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--analyze")analyze=true;else if(a=="--runtime-info")runtimeInfoOnly=true;
            else if(a.rfind("--workers=",0)==0)requestedWorkers=unsigned(std::stoul(a.substr(10)));
            else if(a=="--workers" && i+1<argc)requestedWorkers=unsigned(std::stoul(argv[++i]));}
        api.load();DWORD_PTR p=0,s=0;requireWin(GetProcessAffinityMask(GetCurrentProcess(),&p,&s),"GetProcessAffinityMask(initial)");originalMask=p;systemMask=s;
        if(GetActiveProcessorGroupCount()!=1)throw RuntimeError("multiple processor groups not validated; refusing ambiguous binding",ERROR_NOT_SUPPORTED);
        DWORD bytes=0;GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);std::vector<BYTE> data(bytes);
        requireWin(GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()),&bytes),"GetLogicalProcessorInformationEx");
        std::set<unsigned> classes;
        for(size_t offset=0;offset<bytes;){auto info=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()+offset);
            if(!info->Size || offset+info->Size>bytes)throw RuntimeError("invalid processor topology",ERROR_INVALID_DATA);
            if(info->Relationship==RelationProcessorCore){auto& c=info->Processor;
                if(c.GroupCount!=1 || c.GroupMask[0].Group!=0)throw RuntimeError("unsupported physical-core group mapping",ERROR_NOT_SUPPORTED);
                cores.push_back({unsigned(cores.size()),c.GroupMask[0].Group,c.EfficiencyClass,uint64_t(c.GroupMask[0].Mask),bool(c.Flags&LTP_PC_SMT)});classes.insert(c.EfficiencyClass);}
            offset+=info->Size;}
        if(classes.size()<2)throw RuntimeError("efficiency classes do not distinguish P/E cores",ERROR_NOT_SUPPORTED);
        ULONG size=0;api.getSets(nullptr,0,&size,GetCurrentProcess(),0);std::vector<BYTE> sets(size);
        requireWin(api.getSets(sets.data(),size,&size,GetCurrentProcess(),0),"GetSystemCpuSetInformation");
        for(size_t offset=0;offset<size;){BYTE* b=sets.data()+offset;ULONG len=0,type=0,id=0;WORD group=0;
            std::memcpy(&len,b,4);std::memcpy(&type,b+4,4);if(len<32 || offset+len>size)throw RuntimeError("invalid CPU-set topology",ERROR_INVALID_DATA);
            if(type==0){std::memcpy(&id,b+8,4);std::memcpy(&group,b+12,2);Cpu cpu;cpu.group=group;cpu.logical=b[14];cpu.efficiency=b[18];cpu.setId=id;cpu.allocatedElsewhere=(b[19]&2) && !(b[19]&4);
                auto core=std::find_if(cores.begin(),cores.end(),[&](const Core& c){return c.group==cpu.group && (c.mask&(uint64_t(1)<<cpu.logical));});
                if(core==cores.end() || core->efficiency!=cpu.efficiency)throw RuntimeError("CPU-set/core efficiency disagreement",ERROR_INVALID_DATA);
                cpu.core=core->index;cpuSets.push_back(cpu);}
            offset+=len;}
        unsigned highest=*classes.rbegin();json coreRows=json::array();
        for(const auto& c:cores){json lps=json::array();for(unsigned n=0;n<64;++n)if(c.mask&(uint64_t(1)<<n))lps.push_back(n);
            coreRows.push_back({{"physical_core",c.index},{"group",c.group},{"efficiency_class",c.efficiency},{"classification",c.efficiency==highest?"P":"E"},{"smt",c.smt},{"logical_cpus",lps},{"mask",hexMask(c.mask)}});
            if(c.efficiency==highest){auto options=cpuSets;std::sort(options.begin(),options.end(),[](const Cpu&a,const Cpu&b){return a.logical<b.logical;});
                auto hit=std::find_if(options.begin(),options.end(),[&](const Cpu& x){return x.core==c.index && !x.allocatedElsewhere && (originalMask&(uint64_t(1)<<x.logical));});
                if(hit!=options.end())selected.push_back(*hit);}}
        unsigned available=selected.size();if(!available)throw RuntimeError("no available performance core",ERROR_NOT_SUPPORTED);
        if(requestedWorkers>available)throw RuntimeError("worker count exceeds available physical P cores",ERROR_INVALID_PARAMETER);
        if(requestedWorkers)selected.resize(requestedWorkers);
        std::vector<ULONG> ids;for(const auto& c:selected){processMask|=uint64_t(1)<<c.logical;ids.push_back(c.setId);}
        requireWin(SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS),"SetPriorityClass(HIGH_PRIORITY_CLASS)");
        requireWin(SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(processMask)),"SetProcessAffinityMask(P representatives)");
        requireWin(api.setProcessSets(GetCurrentProcess(),ids.data(),ids.size()),"SetProcessDefaultCpuSets");
        requireWin(GetProcessAffinityMask(GetCurrentProcess(),&p,&s),"GetProcessAffinityMask(verify)");processClass=GetPriorityClass(GetCurrentProcess());
        ULONG gotCount=0;api.getProcessSets(GetCurrentProcess(),nullptr,0,&gotCount);std::vector<ULONG> actualSets(gotCount);
        requireWin(api.getProcessSets(GetCurrentProcess(),actualSets.data(),gotCount,&gotCount),"GetProcessDefaultCpuSets");
        auto sorted=ids;std::sort(sorted.begin(),sorted.end());std::sort(actualSets.begin(),actualSets.end());
        if(p!=processMask || processClass!=HIGH_PRIORITY_CLASS || actualSets!=sorted)throw RuntimeError("process priority/affinity verification mismatch",ERROR_INVALID_DATA);
        char brand[49]{};unsigned a,b,c,d;if(__get_cpuid_max(0x80000000,nullptr)>=0x80000004)for(int leaf=0;leaf<3;++leaf){__cpuid(0x80000002+leaf,a,b,c,d);std::memcpy(brand+leaf*16,&a,4);std::memcpy(brand+leaf*16+4,&b,4);std::memcpy(brand+leaf*16+8,&c,4);std::memcpy(brand+leaf*16+12,&d,4);}
        topology={{"cpu_brand",brand},{"cores",coreRows},{"physical_P_cores_available",available},{"workers",selected.size()},
            {"original_process_mask",hexMask(originalMask)},{"system_mask",hexMask(systemMask)},{"process_mask",hexMask(processMask)},
            {"process_cpu_set_ids",actualSets},{"priority_class",processClass},{"priority_name","HIGH_PRIORITY_CLASS"},{"pid",GetCurrentProcessId()},
            {"classification_source","GetLogicalProcessorInformationEx + GetSystemCpuSetInformation EfficiencyClass; maximum class=P"},
            {"selection_policy","one lowest available logical CPU per physical P core; no E cores or SMT duplicate workers"}};
        workers.reserve(count());for(size_t id=0;id<count();++id)workers.push_back(std::make_unique<Worker>());
        workers[0]->binding=bind(0);workers[0]->threadId=GetCurrentThreadId();workers[0]->ready=workers[0]->ok=true;
    }
    void start(const std::function<void()>& warm){if(started)return;auto begin=Clock::now();started=true;warm();
        for(size_t id=1;id<count();++id)workers[id]->thread=std::thread([&,id,warm]{auto& w=*workers[id];
            try{w.binding=bind(id);w.threadId=GetCurrentThreadId();warm();w.ok=true;}catch(const std::exception&e){w.error=e.what();w.ok=false;}
            {std::lock_guard<std::mutex> l(mutex);w.ready=true;finished.notify_all();}if(!w.ok)return;
            uint64_t seen=0;for(;;){const std::function<void(size_t)>* work=nullptr;{std::unique_lock<std::mutex> l(mutex);wake.wait(l,[&]{return quitting||generation!=seen;});if(quitting)break;seen=generation;work=&job;}
                run(id,*work);{std::lock_guard<std::mutex> l(mutex);--pending;finished.notify_all();}}
        });
        {std::unique_lock<std::mutex> l(mutex);finished.wait(l,[&]{for(const auto&w:workers)if(!w->ready)return false;return true;});}
        for(auto&w:workers)if(!w->ok){std::string e=w->error;shutdown();throw std::runtime_error("worker startup: "+e);}
        startupSeconds=seconds(begin,Clock::now());
    }
    void batch(const std::function<void(size_t)>& f){if(!started)throw std::runtime_error("pool not started");auto start=Clock::now();
        {std::lock_guard<std::mutex> l(mutex);if(pending)throw std::runtime_error("overlapping search batches");job=f;pending=int(count())-1;++generation;}
        wake.notify_all();request.dispatchSeconds+=seconds(start,Clock::now());++request.batches;run(0,f);
        auto wait=Clock::now();{std::unique_lock<std::mutex> l(mutex);finished.wait(l,[&]{return pending==0;});job={};}
        request.joinWaitSeconds+=seconds(wait,Clock::now());
        for(auto&w:workers)if(!w->ok)throw std::runtime_error(w->error);
        if(request.runningJobs.load()!=0)throw std::runtime_error("search work still active after barrier");
    }
    void addSetup(size_t id,double t){workers.at(id)->setupSeconds+=t;}
    json evidence()const{auto out=topology;out["worker_bindings"]=json::array();for(const auto&w:workers)out["worker_bindings"].push_back(w->binding);
        out["thread_pool_startup_seconds"]=startupSeconds;out["persistent_background_threads"]=started && !quitting?count()-1:0;out["joined_threads"]=joinedThreads;return out;}
    json requestEvidence()const{json out={{"configured_workers",count()},{"batches",request.batches},{"tasks_scheduled",request.tasksScheduled},
        {"tasks_completed",request.tasksCompleted},{"incomplete_batches",request.incompleteBatches},{"active_jobs_at_emit",request.runningJobs.load()},
        {"global_budget_node_charges",request.chargedNodes.load()},{"global_node_limit",nullptr},{"max_parallel_search_threads",request.unionPeak},
        {"dispatch_bookkeeping_seconds",request.dispatchSeconds},{"main_join_wait_seconds",request.joinWaitSeconds},
        {"idle_background_threads",started&&!quitting?count()-1:0},{"workers",json::array()}};
        for(size_t id=0;id<count();++id){const auto&w=*workers[id];bool used=w.requestSeen==request.sequence;out["workers"].push_back({{"id",id},
            {"logical_cpu",selected[id].logical},{"physical_core",selected[id].core},{"thread_id",w.threadId},{"affinity_verified",w.ok},
            {"used",used},{"batches",used?w.batches:0},{"cpu_seconds",used && w.cpuSeconds<0?json(nullptr):json(used?w.cpuSeconds:0)},
            {"context_setup_seconds",used?w.setupSeconds:0},{"error",used&&!w.requestError.empty()?json(w.requestError):json(nullptr)}});}
        double cpu=threadCpu();out["main_thread_cpu_seconds"]=cpu>=0&&request.mainCpuStart>=0?json(cpu-request.mainCpuStart):json(nullptr);
        return out;}
    void shutdown(){if(!started||quitting)return;{std::lock_guard<std::mutex> l(mutex);quitting=true;}wake.notify_all();
        for(size_t id=1;id<count();++id)if(workers[id]->thread.joinable()){workers[id]->thread.join();++joinedThreads;}
    }
    ~Runtime(){shutdown();}
};
inline Runtime runtime;
}

#endif


#ifndef TF_PACKAGED_DECISION_TELEMETRY_HPP
#define TF_PACKAGED_DECISION_TELEMETRY_HPP

#include <process.h>

#ifndef TF_PACKAGED_TELEMETRY_BUILD_HPP
#define TF_PACKAGED_TELEMETRY_BUILD_HPP

#ifdef PARALLEL_LOW_BUDGET
#define DT_VERSION "v70-parallel-1p7"
#define DT_BUDGET_ID "1p7s"
#else
#define DT_VERSION "v70-parallel-3p3"
#define DT_BUDGET_ID "3p3s"
#endif

#endif


// Observations only. These values are never read by the policy or its limits.
namespace dt {
using Clock=std::chrono::steady_clock;
struct Record {
    bool active=false,search_executed=false,time_limit=false,node_limit=false,start_gate=false;
    bool interrupted=false,outer_fallback=false,exception=false,analyze=false;
    uint64_t sequence=0,nodes=0,shape_nodes=0,future_call_branches=0,evaluators=0;
    uint64_t eval_calls=0,improve_calls=0;
    uint64_t compact_computations=0,compact_raced_publications=0,hand_score_computations=0;
    int depth=0;
    int64_t requests_count=-1;
    double budget=0,search_seconds=0;
    Clock::time_point started{},search_started{};
    const char* early_reason=nullptr;
    std::string error;
#ifdef DT_COUNTER_ORACLE
    uint64_t oracle_nodes=0,oracle_shape_nodes=0;
#endif
};
inline thread_local Record r;
inline thread_local json constraintInfo;
inline thread_local std::function<json()> constraintDetails;
inline thread_local uint64_t sequence=0;
void beginParallel();void mergeParallel();extern json parallelData;
inline void begin(Clock::time_point started,double budget,bool analyze) {
    constraintInfo=nullptr;constraintDetails={};r=Record{};r.active=true;r.sequence=++sequence;r.started=started;r.budget=budget;r.analyze=analyze;beginParallel();
}
inline void input(const json& q) {
    if(q.is_object() && q.contains("requests") && q["requests"].is_array())
        r.requests_count=static_cast<int64_t>(q["requests"].size());
}
struct SearchScope {
    bool entered=false;
    SearchScope() {
        if(!r.active)return;
        entered=true;r.search_executed=true;
        if(r.depth++==0){r.search_started=Clock::now();prt::request.searchEnter();}
    }
    ~SearchScope() {
        if(entered && --r.depth==0){r.search_seconds+=std::chrono::duration<double>(Clock::now()-r.search_started).count();prt::request.searchLeave();}
    }
};
inline void node(){if(r.active)++r.nodes;}
inline void shape(){if(r.active)++r.shape_nodes;}
inline void futureCall(){if(r.active)++r.future_call_branches;}
inline void evaluator(){if(r.active)++r.evaluators;}
inline void evalCall(){if(r.active)++r.eval_calls;}
inline void improveCall(){if(r.active)++r.improve_calls;}
inline void reason(const char* value){if(r.active)r.early_reason=value;}
inline bool timeLimit(bool value){if(r.active && value)r.time_limit=true;return value;}
inline bool nodeLimit(bool value){if(r.active && value)r.node_limit=true;return value;}
inline bool startGate(bool value){if(r.active && value)r.start_gate=true;return value;}
inline bool startAllowed(bool value){if(r.active && !value)r.start_gate=true;return value;}
inline void interrupted(){if(r.active)r.interrupted=true;}
inline void outerFallback(){if(r.active){r.interrupted=true;r.outer_fallback=true;}}
#ifdef DT_COUNTER_ORACLE
inline void oracle(uint64_t nodes,uint64_t shapes){if(r.active){r.oracle_nodes+=nodes;r.oracle_shape_nodes+=shapes;}}
#endif
inline void emit(const std::string& response,const char* exception=nullptr) noexcept {
    if(!r.active)return;
    // Stop the decision clock BEFORE formatting telemetry. The line is written
    // before the existing stdout response so a reader cannot lose the final
    // record by closing a child immediately after observing that response.
    mergeParallel();
    const double decision=std::chrono::duration<double>(Clock::now()-r.started).count();
    r.active=false;
    try {
        static const std::string session=std::to_string(_getpid())+"-"+
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        const char* why=exception?"exception":!r.error.empty()?"parallel_exception_fallback":r.outer_fallback?"deadline_fallback":
            r.time_limit?"time_limit":r.node_limit?"node_limit":r.interrupted?"search_interrupted":
            r.early_reason?r.early_reason:r.search_executed?"completed_search":"no_search";
        json missing=json::object();
        if(r.requests_count<0)missing["requests_count"]="invalid_json_or_no_requests_array";
        if(r.depth!=0)missing["search_seconds"]="search_scope_not_closed";
        json out={{"type","decision_telemetry"},{"schema_version",1},
            {"decision_id",std::string(DT_VERSION)+":"+session+":"+std::to_string(r.sequence)+":"+
                (r.requests_count<0?"null":std::to_string(r.requests_count))},
            {"request_sequence",r.sequence},{"requests_count",r.requests_count<0?json(nullptr):json(r.requests_count)},
            {"version",DT_VERSION},{"budget_id",DT_BUDGET_ID},{"search_budget_seconds",r.budget},
            {"response",response},{"decision_seconds",decision},
            {"search_seconds",r.depth?json(nullptr):json(r.search_seconds)},
            {"nodes",r.nodes},{"shape_nodes",r.shape_nodes},
            {"future_call_branches",r.future_call_branches},
            {"legacy_node_charges",r.nodes+r.shape_nodes+r.future_call_branches},
            {"evaluators",r.evaluators},{"eval_calls",r.eval_calls},{"improve_calls",r.improve_calls},
            {"search_executed",r.search_executed},{"exit_reason",why},
            {"stop_flags",{{"time_limit",r.time_limit},{"node_limit",r.node_limit},
                {"start_budget_gate",r.start_gate},{"interrupted",r.interrupted},{"outer_fallback",r.outer_fallback}}},
            {"analyze",r.analyze},{"null_reasons",missing},{"parallel",parallelData}};
        if(!constraintInfo.is_null())out["opponent_hand_constraints"]=constraintInfo;
        if(exception)out["exception"]=std::string(exception).substr(0,240);
        if(!r.error.empty())out["parallel_error"]=r.error;
#ifdef DT_COUNTER_ORACLE
        out["counter_oracle"]={{"local_node_sum",r.oracle_nodes},{"local_shape_sum",r.oracle_shape_nodes},
            {"ok",r.oracle_nodes==r.nodes+r.shape_nodes+r.future_call_branches && r.oracle_shape_nodes==r.shape_nodes}};
#endif
        std::string line=out.dump()+"\n";std::cerr.write(line.data(),static_cast<std::streamsize>(line.size()));
        if(constraintDetails)try{auto detail=constraintDetails();detail["decision_id"]=out["decision_id"];std::cerr<<detail.dump()<<'\n';}catch(...){std::fputs("{\"type\":\"opponent_hand_constraints_analysis_error\",\"reason\":\"serialization_failed\"}\n",stderr);}
    } catch(...) {
        // Only a diagnostic failure; do not turn a valid policy response into
        // PASS. A failed record is explicit and contains no fabricated counts.
        // Bounded allocation-free fallback. All normal action strings use this
        // ASCII alphabet; an unexpected string is explicitly unavailable.
        char safeResponse[192]="null",count[32]="null",line[2048];
        bool ascii=response.size()<180;
        for(unsigned char c:response)ascii&=(c==' ' || c=='_' || c>='0' && c<='9' || c>='A' && c<='Z');
        if(ascii)std::snprintf(safeResponse,sizeof(safeResponse),"\"%s\"",response.c_str());
        if(r.requests_count>=0)std::snprintf(count,sizeof(count),"%lld",static_cast<long long>(r.requests_count));
        int size=std::snprintf(line,sizeof(line),
            "{\"type\":\"decision_telemetry\",\"schema_version\":1,\"decision_id\":\"%s:%d:fallback:%llu\","
            "\"request_sequence\":%llu,\"requests_count\":%s,\"version\":\"%s\",\"budget_id\":\"%s\","
            "\"search_budget_seconds\":%.6f,\"response\":%s,\"decision_seconds\":%.9f,"
            "\"search_seconds\":null,\"nodes\":null,\"shape_nodes\":null,\"search_executed\":%s,"
            "\"exit_reason\":\"telemetry_serialization_failure\","
            "\"null_reasons\":{\"telemetry\":\"primary_record_serialization_failed\"}}\n",
            DT_VERSION,_getpid(),static_cast<unsigned long long>(r.sequence),static_cast<unsigned long long>(r.sequence),
            count,DT_VERSION,DT_BUDGET_ID,r.budget,safeResponse,decision,r.search_executed?"true":"false");
        if(size>0 && size<int(sizeof(line)))std::fwrite(line,1,size,stderr);
    }
}
}

#endif


#ifndef TF_PACKAGED_PARALLEL_TELEMETRY_HPP
#define TF_PACKAGED_PARALLEL_TELEMETRY_HPP

// Included after decision_telemetry.hpp. Workers never emit action telemetry.
namespace dt {
inline std::vector<Record> workerRecords;
struct Layer {const char* kind;int progress,shape,scheduled,completed;bool committed;double seconds;};
inline std::vector<Layer> layers;
inline std::unordered_set<uint64_t> visitedRootJobs;
inline unsigned rootJobRevisits=0,duplicateJobs=0;
inline double mainSnapshotSeconds=0;
inline uint64_t compactComputations=0; // coordinator-only aggregate after join
inline void beginParallel(){
    prt::request.begin(r.sequence,r.started,r.budget);
    workerRecords.assign(prt::runtime.count(),Record{});
    layers.clear();
    visitedRootJobs.clear();rootJobRevisits=duplicateJobs=0;
    mainSnapshotSeconds=0;
}
inline void beginWorker(uint64_t sequence,Clock::time_point start,double budget){
    r=Record{};r.active=true;r.sequence=sequence;r.started=start;r.budget=budget;
}
inline void saveWorker(size_t worker){if(worker)workerRecords.at(worker)=r;}
inline json parallelData;
inline void mergeParallel(){
    if(!prt::request.active)return;
    if(prt::request.runningJobs.load()!=0 || prt::request.unionActive!=0)
        throw std::runtime_error("cannot publish while parallel work remains active");
    parallelData=prt::runtime.requestEvidence();double workerTimeSum=r.search_seconds;
    json work=json::array();
    work.push_back({{"worker",0},{"nodes",r.nodes},{"shape_nodes",r.shape_nodes},{"search_seconds_thread_wall",r.search_seconds}});
    for(size_t i=1;i<workerRecords.size();++i){const auto& w=workerRecords[i];bool used=w.sequence==r.sequence;
        work.push_back({{"worker",i},{"nodes",used?w.nodes:0},{"shape_nodes",used?w.shape_nodes:0},{"search_seconds_thread_wall",used?w.search_seconds:0}});
        if(!used)continue;
        r.nodes+=w.nodes;r.shape_nodes+=w.shape_nodes;r.future_call_branches+=w.future_call_branches;r.evaluators+=w.evaluators;
        r.eval_calls+=w.eval_calls;r.improve_calls+=w.improve_calls;r.search_executed|=w.search_executed;
        r.time_limit|=w.time_limit;r.node_limit|=w.node_limit;r.start_gate|=w.start_gate;r.interrupted|=w.interrupted;
        r.compact_computations+=w.compact_computations;r.compact_raced_publications+=w.compact_raced_publications;
        r.hand_score_computations+=w.hand_score_computations;
        workerTimeSum+=w.search_seconds;
#ifdef DT_COUNTER_ORACLE
        r.oracle_nodes+=w.oracle_nodes;r.oracle_shape_nodes+=w.oracle_shape_nodes;
#endif
    }
    r.time_limit|=prt::request.timeStop.load();
    r.search_seconds=prt::request.unionSeconds;
    parallelData["worker_work"]=work;parallelData["thread_search_wall_sum"]=workerTimeSum;
    parallelData["search_wall_union_seconds"]=r.search_seconds;
    parallelData["compact_computations"]=r.compact_computations;parallelData["compact_raced_publications"]=r.compact_raced_publications;
    parallelData["hand_score_computations"]=r.hand_score_computations;
    parallelData["duplicate_jobs_within_batch"]=duplicateJobs;
    parallelData["root_job_revisits_across_layers"]=rootJobRevisits;
    parallelData["coordinator_snapshot_seconds"]=mainSnapshotSeconds;
    parallelData["layers"]=json::array();for(const auto& l:layers)parallelData["layers"].push_back({{"kind",l.kind},{"progress_depth",l.progress},
        {"shape_depth",l.shape},{"scheduled",l.scheduled},{"completed",l.completed},{"committed",l.committed},{"batch_wall_seconds",l.seconds}});
}
}

#endif


#ifndef TF_PACKAGED_STANDING_HPP
#define TF_PACKAGED_STANDING_HPP

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

#endif


#ifndef TF_PACKAGED_PAYOUTS_HPP
#define TF_PACKAGED_PAYOUTS_HPP

namespace mj {
struct OpponentPayouts {
    static constexpr int COUNT=21;
    // Exact standard ron payments, paired by the same underlying hand value.
    // Pooled shape from 28 development walls, balanced by wall/player-hand.
    // Details and model-label limitations: research/payout-distribution-002.
    array<int,COUNT> points;
    array<double,COUNT> probability{};
    explicit OpponentPayouts(double publicMean,bool dealer) {
        points=dealer?array<int,COUNT>{{1500,2000,2400,2900,3400,3900,4400,4800,5300,5800,6800,7700,8700,9600,10600,11600,12000,18000,24000,36000,48000}}:
                      array<int,COUNT>{{1000,1300,1600,2000,2300,2600,2900,3200,3600,3900,4500,5200,5800,6400,7100,7700,8000,12000,16000,24000,32000}};
        const array<double,COUNT> shape{{0.034249983220076094,0.13588752785313885,0.015333769205622752,0.12935097817382543,0.002441742654508612,0.16290809422279226,0.00095238095238095238,0.012372686511496497,0.00095238095238095238,0.1698549198118568,0.00095238095238095238,0.075237705078298578,0.00095238095238095238,0.0091703464172490975,0.00095238095238095238,0.11136521276332567,0.091967780638704089,0.033406695755826471,0.0063712568802706659,0.00095238095238095238,0.0043670150987224158}};
        double target=clamp(publicMean,double(points.front()),double(points.back()));
        array<double,COUNT> logs;
        for(int i=0;i<COUNT;++i) logs[i]=log(double(points[i])/points.front());
        auto distribution=[&](double tilt) {
            double total=0,mean=0,shift=tilt>0?tilt*logs.back():0;
            for(int i=0;i<COUNT;++i) {
                probability[i]=shape[i]*exp(tilt*logs[i]-shift);
                total+=probability[i];
            }
            for(int i=0;i<COUNT;++i) { probability[i]/=total; mean+=probability[i]*points[i]; }
            return mean;
        };
        // Power tilting changes spread around the old public expected amount;
        // it does not silently replace the old mean with a fitted larger mean.
        double lo=-80,hi=80;
        for(int k=0;k<56;++k) {
            double mid=(lo+hi)*.5;
            if(distribution(mid)<target) lo=mid; else hi=mid;
        }
        distribution((lo+hi)*.5);
    }
    double mean() const {
        double result=0;
        for(int i=0;i<COUNT;++i) result+=points[i]*probability[i];
        return result;
    }
    json explain() const {
        json branches=json::array();
        for(int i=0;i<COUNT;++i) branches.push_back({{"points",points[i]},{"probability",probability[i]}});
        return {{"branches",branches},{"mean_points",mean()}};
    }
};
}

#endif


#ifndef TF_PACKAGED_IMMEDIATE_HPP
#define TF_PACKAGED_IMMEDIATE_HPP

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

#endif


#ifndef TF_PACKAGED_IMPROVEMENT_HPP
#define TF_PACKAGED_IMPROVEMENT_HPP

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

#endif


#ifndef TF_PACKAGED_URA_HPP
#define TF_PACKAGED_URA_HPP

namespace mj {
inline constexpr double IPPATSU_WIN_PRIOR=0.32000000000000001;
struct ExpectedBonuses {
    static constexpr int MAX_INDICATORS=5, MAX_URA=20, MAX_EXTRA=21;
    array<double,MAX_URA+1> ura{};
    array<double,MAX_EXTRA+1> extra{};
    array<int,5> groups{};
    int indicators=0,unknown=0;
    double ippatsu=0;
    static uint64_t inventoryKey(const array<int,5>& counts,int n) {
        if(n<0 || n>MAX_INDICATORS) throw runtime_error("invalid ura indicator count");
        uint64_t key=uint64_t(n)<<40;
        for(int i=0;i<5;++i) {
            if(counts[i]<0 || counts[i]>136) throw runtime_error("invalid ura inventory bin");
            key|=uint64_t(counts[i])<<(8*i);
        }
        return key;
    }
    static double choose(int n,int k) {
        if(k<0 || k>n) return 0;
        double out=1;
        for(int i=1;i<=k;++i) out*=double(n-i+1)/i;
        return out;
    }
    ExpectedBonuses(const array<int,5>& counts,int n,double ip=IPPATSU_WIN_PRIOR):groups(counts),indicators(n),ippatsu(ip) {
        inventoryKey(counts,n);
        unknown=accumulate(counts.begin(),counts.end(),0);
        if(unknown>136 || n>unknown || !isfinite(ip) || ip<0 || ip>1)
            throw runtime_error("invalid expected-bonus distribution input");
        if(n==0) ura[0]=1;
        else if(n==1) {
            for(int h=0;h<=4;++h) ura[h]=double(counts[h])/unknown;
        } else {
            // Each physical unknown tile is an equiprobable indicator. Group
            // by the number of matching dora in our complete winning hand.
            // Coefficients count subsets, so indicators are drawn WITHOUT replacement.
            array<array<double,MAX_URA+1>,MAX_INDICATORS+1> dp{},next{};
            dp[0][0]=1;
            for(int value=0;value<=4;++value) {
                next={}; array<double,MAX_INDICATORS+1> ways{};
                for(int take=0;take<=min(n,counts[value]);++take) ways[take]=choose(counts[value],take);
                for(int used=0;used<=n;++used) for(int han=0;han<=4*used;++han) if(dp[used][han]>0)
                    for(int take=0;take<=min(n-used,counts[value]);++take)
                        next[used+take][han+value*take]+=dp[used][han]*ways[take];
                dp=next;
            }
            double denominator=choose(unknown,n);
            for(int h=0;h<=4*n;++h) ura[h]=dp[n][h]/denominator;
        }
        double sum=accumulate(ura.begin(),ura.end(),0.0);
        if(!isfinite(sum) || abs(sum-1)>1e-10) throw runtime_error("ura probability normalization failed");
        for(int h=0;h<=MAX_URA;++h) {
            ura[h]/=sum;
            extra[h]+=(1-ip)*ura[h]; extra[h+1]+=ip*ura[h];
        }
    }
    json explain() const {
        double mean=0,tail=0;
        for(int h=0;h<=MAX_URA;++h) { mean+=h*ura[h]; if(h>=3) tail+=ura[h]; }
        return {{"indicator_value_bins",groups},{"unknown_physical_tiles",unknown},{"indicators",indicators},
                {"ura_probabilities",ura},{"extra_han_probabilities",extra},{"mean_ura",mean},
                {"ura_at_least_three",tail},{"ippatsu_given_win_prior",ippatsu}};
    }
};
struct BonusScoreKey {
    uint64_t shape=0,inventory=0;
    bool lastNormalTile=false;
    bool operator==(const BonusScoreKey& other) const {
        return shape==other.shape && inventory==other.inventory && lastNormalTile==other.lastNormalTile;
    }
};
struct BonusScoreHash {
    size_t operator()(const BonusScoreKey& key) const {
        uint64_t x=key.inventory+0x9e3779b97f4a7c15ULL;
        x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;
        return key.shape^(x^(x>>31))^(key.lastNormalTile?0x632be59bd9b4e019ULL:0);
    }
};
}

#endif


#ifndef TF_PACKAGED_RON_SOURCES_HPP
#define TF_PACKAGED_RON_SOURCES_HPP

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

#endif


#ifndef TF_PACKAGED_END_WALL_HPP
#define TF_PACKAGED_END_WALL_HPP

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

#endif


#ifndef TF_PACKAGED_OTHER_WINS_HPP
#define TF_PACKAGED_OTHER_WINS_HPP

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

#endif


#ifndef TF_PACKAGED_EVENT_HAZARDS_HPP
#define TF_PACKAGED_EVENT_HAZARDS_HPP

namespace mj {
struct PublicHazardPlayer {
    bool locked=false,open=false,canDeclare=false;
    int turn=0,melds=0;
    double ready=0;
};
struct PublicHazardProjection {
    array<double,4> silent{},locked{};
    array<int,4> completed{},eligible{};
    json explain() const {
        return {{"silent_pressure",silent},{"locked_probability",locked},
                {"completed_normal_turns",completed},{"eligible_declaration_turns",eligible}};
    }
};
struct TemporalOtherWins {
    static constexpr int MAX_TIME=72;
    static constexpr double TSUMO_UNLOCKED=.0345,TSUMO_LOCKED=.1074;
    static constexpr double RON_UNLOCKED=.0213,RON_LOCKED=.0900;
    static constexpr double DECLARATION_RATE=.735,PROGRESS_RATE=.024;
    struct Cell {
        double survival=1;array<double,2> stopped{};
        double tsumoSurvival=1,silentTsumoSurvival=1,lockedTsumoSurvival=1;
        array<double,2> tsumoStopped{};
    };
    struct OrderedStep {
        double survival=1,tsumoSurvival=1,scale=1;
        array<double,2> tsumoStopped{},ronStopped{};
    };
    array<PublicHazardPlayer,4> players{};
    array<array<array<double,2>,4>,16> tsumoValue{};
    array<array<array<array<double,2>,4>,4>,16> ronValue{};
    array<array<Cell,4>,MAX_TIME> timeline{};
    int self=0,rootActor=0,remaining=0;
    bool replacement=false;
    double fullSurvival=1;
    static double silentAtTurn(const PublicHazardPlayer& p,int turn) {
        if(p.melds==0) return .008+.0015*turn+.00019*turn*turn;
        const double intercept[]={0,-3.65,-2.45,-1.1,-.3};
        return 1/(1+exp(-(intercept[min(4,p.melds)]+.23*turn)));
    }
    static int turnsThrough(int afterActor,int who,int draws) {
        int distance=(who-afterActor+4)%4;if(!distance)distance=4;
        return draws>=distance?1+(draws-distance)/4:0;
    }
    PublicHazardProjection projectCounts(const array<int,4>& completed,const array<int,4>& eligible) const {
        PublicHazardProjection out;out.completed=completed;out.eligible=eligible;
        for(int p=0;p<4;++p) {
            const auto& state=players[p];
            double base=clamp(max(state.ready,silentAtTurn(state,state.turn+completed[p])),0.,1.);
            out.silent[p]=1-(1-base)*exp(-PROGRESS_RATE*completed[p]);
            if(state.locked) out.locked[p]=1;
            else if(!state.open && state.canDeclare) {
                double exposure=0;
                for(int j=0;j<eligible[p];++j)
                    exposure+=max(state.ready,silentAtTurn(state,state.turn+j));
                out.locked[p]=-expm1(-DECLARATION_RATE*exposure);
            }
        }
        return out;
    }
    PublicHazardProjection project(int elapsed) const {
        int normal=max(0,elapsed-(replacement?1:0));
        int legal=min(normal,max(0,remaining-4-(replacement?1:0)));
        array<int,4> completed{},eligible{};
        for(int p=0;p<4;++p) {
            completed[p]=turnsThrough(rootActor,p,normal);
            eligible[p]=turnsThrough(rootActor,p,legal);
        }
        return projectCounts(completed,eligible);
    }
    void addTsumo(int winner,double mass,const array<double,2>& value) {
        for(unsigned mask=0;mask<16;++mask)addTsumo(mask,winner,mass,value);
    }
    void addRon(int winner,int payer,double mass,const array<double,2>& value) {
        for(unsigned mask=0;mask<16;++mask)addRon(mask,winner,payer,mass,value);
    }
    void addTsumo(unsigned mask,int winner,double mass,const array<double,2>& value) {
        for(int b=0;b<2;++b) tsumoValue[mask][winner][b]+=mass*value[b];
    }
    void addRon(unsigned mask,int winner,int payer,double mass,const array<double,2>& value) {
        for(int b=0;b<2;++b) ronValue[mask][winner][payer][b]+=mass*value[b];
    }
    Cell atProjection(int actor,const PublicHazardProjection& q) const {
        Cell out;if(actor==self)return out;out.survival=out.tsumoSurvival=0;
        out.silentTsumoSurvival=exp(-TSUMO_UNLOCKED*q.silent[actor]);
        out.lockedTsumoSurvival=exp(-TSUMO_LOCKED);
        // Sum joint public-state scenarios. Winner payments, all deposits and
        // event probabilities must refer to the SAME future declarations.
        // Factoring the independent masks gives the previous survival rate;
        // values cannot be factored across nonlinear point/rank thresholds.
        for(unsigned mask=0;mask<16;++mask) {
            double branch=1;
            for(int p=0;p<4;++p)branch*=mask&(1u<<p)?q.locked[p]:1-q.locked[p];
            if(branch<=0)continue;
            bool locked=mask&(1u<<actor);
            double tsumoRate=(locked?TSUMO_LOCKED:TSUMO_UNLOCKED)*(locked?1:q.silent[actor]);
            double ronRate=locked?RON_LOCKED:RON_UNLOCKED;
            double tsumoSurvival=exp(-tsumoRate),ronSurvival=1,totalHit=0;
            array<double,4> hit{};
            for(int winner=0;winner<4;++winner) if(winner!=self && winner!=actor) {
                double survive=exp(-ronRate*((mask&(1u<<winner))?1:q.silent[winner]));
                hit[winner]=1-survive;totalHit+=hit[winner];ronSurvival*=survive;
            }
            out.survival+=branch*tsumoSurvival*ronSurvival;
            out.tsumoSurvival+=branch*tsumoSurvival;
            double tsumoWin=1-tsumoSurvival,ronWin=tsumoSurvival*(1-ronSurvival);
            for(int b=0;b<2;++b) {
                double ron=0;
                if(totalHit>0) for(int winner=0;winner<4;++winner)
                    ron+=hit[winner]/totalHit*ronValue[mask][winner][actor][b];
                // Retain a pooled single-winner value when simultaneous ron
                // is possible. The event probability is for at least one ron.
                out.stopped[b]+=branch*(tsumoWin*tsumoValue[mask][actor][b]+ronWin*ron);
                out.tsumoStopped[b]+=branch*tsumoWin*tsumoValue[mask][actor][b];
            }
        }
        out.survival=clamp(out.survival,0.,1.);
        return out;
    }
    void configure(int who,int actor,int tiles,bool replacementDraw,const array<PublicHazardPlayer,4>& states) {
        self=who;rootActor=actor>=0 && actor<4?actor:who;remaining=tiles;replacement=replacementDraw;players=states;
        for(int t=0;t<MAX_TIME;++t) {
            auto projected=project(t);
            for(int p=0;p<4;++p) timeline[t][p]=atProjection(p,projected);
        }
        fullSurvival=1;
        for(int p=0;p<4;++p) if(p!=self) fullSurvival*=timeline[0][p].survival;
    }
    OrderedStep orderedStep(int actor,int elapsed,double targetSurvival=-1) const {
        const auto& cell=timeline[clamp(elapsed,0,MAX_TIME-1)];
        const auto& event=cell[actor];OrderedStep out;
        double power=1;
        if(targetSurvival>=0 && targetSurvival!=fullSurvival && fullSurvival>0 && fullSurvival<1)
            power=log(clamp(targetSurvival,1e-12,1.))/log(fullSurvival);
        out.survival=power==1?event.survival:pow(event.survival,power);
        out.scale=power==1?1:(event.survival<1?(1-out.survival)/(1-event.survival):0);
        out.tsumoSurvival=1-out.scale*(1-event.tsumoSurvival);
        for(int b=0;b<2;++b) {
            out.tsumoStopped[b]=out.scale*event.tsumoStopped[b];
            out.ronStopped[b]=out.scale*(event.stopped[b]-event.tsumoStopped[b]);
        }
        return out;
    }
    OtherWinInterval interval(int who,int afterActor,int draws,double targetSurvival=-1,int elapsed=0) const {
        if(who!=self)throw runtime_error("opponent hazard observer mismatch");
        OtherWinInterval out;if(afterActor<0 || afterActor>=4)afterActor=self;
        double power=1;
        if(targetSurvival>=0 && targetSurvival!=fullSurvival && fullSurvival<1)
            power=log(clamp(targetSurvival,1e-12,1.))/log(fullSurvival);
        for(int step=1;step<=draws;++step) {
            int actor=(afterActor+step)%4;if(actor==self)continue;
            const auto& cell=timeline[clamp(elapsed+step-1,0,MAX_TIME-1)][actor];
            double survive=power==1?cell.survival:pow(cell.survival,power);
            double scale=power==1?1:(cell.survival<1?(1-survive)/(1-cell.survival):0);
            for(int b=0;b<2;++b) out.stopped[b]+=out.survival*scale*cell.stopped[b];
            out.survival*=survive;
        }
        return out;
    }
    json explain() const {
        array<double,4> survival{};array<array<double,2>,4> values{};
        for(int p=0;p<4;++p) {
            survival[p]=timeline[0][p].survival;
            if(survival[p]<1) for(int b=0;b<2;++b) values[p][b]=timeline[0][p].stopped[b]/(1-survival[p]);
        }
        json projected=json::array();
        for(int time:{0,4,12,24}) if(time<=remaining)projected.push_back({{"elapsed_normal_clock",time},{"state",project(time).explain()}});
        return {{"mode","public_time_with_joint_declaration_values"},{"actor_survival",survival},{"actor_values",values},
            {"full_cycle_survival",fullSurvival},{"projections",projected},
            {"tsumo_unlocked",TSUMO_UNLOCKED},{"tsumo_locked",TSUMO_LOCKED},
            {"ron_unlocked",RON_UNLOCKED},{"ron_locked",RON_LOCKED},
            {"declaration_rate",DECLARATION_RATE},{"progress_rate",PROGRESS_RATE}};
    }
};
}

#endif


#ifndef TF_PACKAGED_DECLARATION_VALUES_HPP
#define TF_PACKAGED_DECLARATION_VALUES_HPP

namespace mj {
// All branches use the original public reference, even when a hypothetical
// declaration has deducted a new stick. Re-referencing each mask would erase
// the very score/rank movement that the forecast is intended to evaluate.
struct DeclarationValues {
    using Values=array<double,16>;
    int self=0;
    unsigned known=0,possible=0;
    double reference=0;
    vector<unsigned> masks;
    unordered_map<uint64_t,Values> ownCache;
    void reset(int who,unsigned current,unsigned mayDeclare) {
        self=who;known=current;possible=mayDeclare&~known&~(1u<<self);
        reference=PositionValue(self,known).reference();
        masks.clear();ownCache.clear();
        for(unsigned mask=0;mask<16;++mask)
            if((mask&known)==known && !(mask&~(known|possible))) masks.push_back(mask);
    }
    Values weights(const PublicHazardProjection& projected) const {
        Values out{};double total=0;
        for(unsigned mask:masks) {
            double p=1;
            for(int player=0;player<4;++player)
                p*=mask&(1u<<player)?projected.locked[player]:1-projected.locked[player];
            out[mask]=p;total+=p;
        }
        if(abs(total-1)>1e-9) throw runtime_error("future declaration masks do not conserve probability");
        return out;
    }
    double win(unsigned mask,int winner,int payer,int payment,bool tsumo,bool ownRiichi=false) const {
        PositionValue pos(self,mask);
        auto sc=pos.settled(winner,payer,payment,tsumo,ownRiichi);
        return standing.value(sc,standing.after(sc,winner),self)-reference;
    }
    const Values& ownWin(int payment,bool tsumo,bool ownRiichi,int payer=-1) {
        uint64_t key=(uint64_t(payment)<<8)|(unsigned(tsumo)<<6)|(unsigned(ownRiichi)<<5)|unsigned(payer+2);
        auto found=ownCache.find(key);if(found!=ownCache.end())return found->second;
        Values value{};
        for(unsigned mask:masks)value[mask]=win(mask,self,payer,payment,tsumo,ownRiichi);
        return ownCache.emplace(key,value).first->second;
    }
    double average(const Values& value,const Values& probability) const {
        double out=0;for(unsigned mask:masks)out+=probability[mask]*value[mask];return out;
    }
    double nagashi(bool selfReady,const PublicHazardProjection& projected) const {
        auto probability=weights(projected);double out=0;
        for(unsigned mask:masks)if(probability[mask]>0) {
            PositionValue pos(self,mask);unsigned required=mask;
            if(selfReady)required|=1u<<self;
            for(unsigned ready=0;ready<16;++ready) {
                if((ready&required)!=required || (!selfReady && !(mask&(1u<<self)) && (ready&(1u<<self))))continue;
                double p=probability[mask];
                for(int player=0;player<4;++player)if(player!=self && !(mask&(1u<<player)))
                    p*=ready&(1u<<player)?projected.silent[player]:1-projected.silent[player];
                if(p>0)out+=p*(pos.nagashiValue(ready)-reference);
            }
        }
        return out;
    }
    double draw(bool selfReady,bool ownRiichi,const PublicHazardProjection& projected) const {
        auto probability=weights(projected);double out=0;
        for(unsigned mask:masks) if(probability[mask]>0) {
            PositionValue pos(self,mask);
            unsigned required=mask;
            if(selfReady || ownRiichi)required|=1u<<self;
            for(unsigned ready=0;ready<16;++ready) {
                if((ready&required)!=required || (!selfReady && !ownRiichi && !(mask&(1u<<self)) && (ready&(1u<<self))))continue;
                double p=probability[mask];
                for(int player=0;player<4;++player) if(player!=self && !(mask&(1u<<player)))
                    p*=ready&(1u<<player)?projected.silent[player]:1-projected.silent[player];
                if(p>0)out+=p*(pos.drawValue(ready,ownRiichi)-reference);
            }
        }
        return out;
    }
};
}

#endif


#ifndef TF_PACKAGED_WIN_ORDER_HPP
#define TF_PACKAGED_WIN_ORDER_HPP

namespace mj {
struct WinScore;
struct WinOffer {
    double probability=0,gain=0;
    void scale(double factor){probability*=factor;gain*=factor;}
};
struct OrderedWinInterval {
    const DeclarationValues* payouts=nullptr;
    int opponents=0,elapsed=0,ownElapsed=0;
    bool ownDraw=false,committed=false;
    array<int,3> actors{};
    array<double,4> normalSources{},extraSources{};
    array<TemporalOtherWins::OrderedStep,3> other{};
    DeclarationValues::Values extraWeights{},ownWeights{};
    array<DeclarationValues::Values,3> noTsumoWeights{};
    WinOffer extra,tsumo;
    array<WinOffer,3> ron{};
    void addTsumo(double probability,const WinScore& score);
    void addRon(double probability,const WinScore& ordinary,const WinScore& last,bool lastOrdinary,const array<double,4>* release=nullptr);
    void cap() {
        double total=extra.probability+tsumo.probability;
        for(int i=0;i<opponents;++i)total+=ron[i].probability;
        if(total>.65) {
            double scale=.65/total;extra.scale(scale);tsumo.scale(scale);
            for(auto& offer:ron)offer.scale(scale);
        }
    }
    retention::Value settle(double continuation,double continuingWin,double discardTax=0,double discardSurvival=1,double continuingRisk=0) const {
        double value=continuation,win=continuingWin,risk=continuingRisk;
        // The cost belongs to a discard AFTER a failed own draw, not to
        // branches on which somebody ended the hand before we could draw.
        if(ownDraw) {
            value=tsumo.gain+(1-tsumo.probability)*(discardSurvival*value-discardTax);
            win=tsumo.probability+(1-tsumo.probability)*discardSurvival*win;
            risk=(1-tsumo.probability)*(1-discardSurvival+discardSurvival*risk);
        }
        for(int i=opponents-1;i>=0;--i) {
            const auto& event=other[i];const auto& offer=ron[i];int branch=committed?1:0;
            value=event.tsumoStopped[branch]+offer.gain+
                (1-offer.probability)*(event.ronStopped[branch]+event.survival*value);
            risk*=(1-offer.probability)*event.survival;
            win=event.tsumoSurvival*offer.probability+
                (1-offer.probability)*event.survival*win;
        }
        // A small pooled call-discard opportunity is placed before the next
        // normal draw. It never receives houtei and is not placed after an
        // already-terminal last draw. Its frequency remains approximate.
        value=extra.gain+(1-extra.probability)*value;
        win=extra.probability+(1-extra.probability)*win;
        return {value,win,(1-extra.probability)*risk};
    }
};
}

#endif


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

// Included in namespace mj after deadlineResponse/deadlineSnapshot declarations.
inline double retentionUnit(){return standing.enabled?max(1.,standing.returnPoints/25.):1000.;}
inline thread_local bool retentionBaselineMode=false;
#ifdef RETENTION_FAULT_TEST
inline thread_local int retentionFaultMode=0,retentionFaultCounter=0;
inline void retentionFaultPoint(int phase){
    if((phase==1 && retentionFaultMode==1) || (phase==2 && !retentionBaselineMode && retentionFaultMode==2 && ++retentionFaultCounter==1))
        prt::request.deadline=prt::Clock::now()-chrono::milliseconds(1);
}
#endif
struct RetentionAlternative {
    string action,kind="attack",tileName,probabilityBasis="computed_continuation";
    string riskBasis="modeled_current_hand";
    int maximumOwnDraws=-1,origin=-1,raw=-1,sh=9,uke=0,depth=0;
    double base=0,off=0,conditionalWin=0,conditionalRisk=0,survival=1,loss=0,switchCost=0;
    double shapeAdjustment=0,redAdjustment=0,shantenAdjustment=0,reserveAdjustment=0,closedAdjustment=0;
    bool known=true,riskKnown=true,immediateRiskKnown=true,complete=true,riichi=false,hasImmediateDiscard=false;
    string effectiveRiskBasis()const{return !standing.enabled && riskBasis=="modeled_current_hand"?"current_discard_only_legacy":riskBasis;}
    retention::Entry entry()const{
        const auto riskScope=effectiveRiskBasis();
        double risk=riskScope=="current_discard_only_legacy"?1-survival:1-survival+survival*conditionalRisk;
        bool rk=immediateRiskKnown && (riskKnown || riskScope=="current_discard_only_legacy");
        return {base-switchCost,survival*conditionalWin,known,complete,risk,rk};
    }
    json explain(double unit)const{
        auto e=entry();const bool wk=e.validWin(),rk=e.validRisk();const auto riskScope=effectiveRiskBasis();
        double baseSurvival=standing.enabled?survival:1.;
        double adjustments=shapeAdjustment+redAdjustment+shantenAdjustment+reserveAdjustment+closedAdjustment;
        double currentPenalty=immediateRiskKnown?retention::DEAL_IN_UNITS*unit*(1-survival):0;
        double futurePenalty=riskKnown && riskScope!="current_discard_only_legacy"?retention::DEAL_IN_UNITS*unit*survival*conditionalRisk:0;
        return {{"action",action},{"policy",kind},{"tile",tileName},{"origin",origin},
            {"original_candidate_score",base},{"switch_cost",switchCost},{"base_score",e.base},{"off",off},
            {"base_components",{{"conditional_continuation",off},{"current_survival_multiplier",baseSurvival},
                {"continuation_lost_on_current_ron",(1-baseSurvival)*off},{"current_expected_loss",loss},
                {"shape_adjustment",shapeAdjustment},{"red_discard_adjustment",redAdjustment},{"shanten_adjustment",shantenAdjustment},
                {"reserve_adjustment",reserveAdjustment},{"closed_hand_adjustment",closedAdjustment},{"switch_cost",switchCost},
                {"remaining_base_adjustment",base-(baseSurvival*off-loss+adjustments)}}},
            {"win",wk?json(conditionalWin):json(nullptr)},{"win_after_current_risk",wk?json(e.win):json(nullptr)},
            {"current_p_ron",hasImmediateDiscard && immediateRiskKnown?json(1-survival):json(nullptr)},
            {"current_loss",loss},{"future_deal_in_conditional",riskKnown && riskScope!="current_discard_only_legacy"?json(conditionalRisk):json(nullptr)},
            {"forecast_deal_in",rk?json(e.dealIn):json(nullptr)},{"risk_probability_scope",riskScope},
            {"win_completion_utility",wk?json(retention::winReward(e,unit)):json(nullptr)},
            {"deal_in_preference_cost",rk?json(retention::riskPenalty(e,unit)):json(nullptr)},
            {"current_deal_in_preference_cost",immediateRiskKnown?json(currentPenalty):json(nullptr)},
            {"future_deal_in_preference_cost",riskKnown && riskScope!="current_discard_only_legacy"?json(futurePenalty):json(nullptr)},
            {"win_utility_lower_bound",retention::winReward(e,unit)},
            {"risk_cost_upper_bound",retention::riskPenalty(e,unit)},
            {"unknown_risk_reserve",rk?0:max(0.,retention::riskPenalty(e,unit)-currentPenalty-futurePenalty)},
            {"score",retention::objective(e,unit)},{"score_is_lower_bound",!wk || !rk},
            {"probability_status",wk?"estimated":"not_computed"},{"probability_basis",probabilityBasis},
            {"maximum_own_draws",maximumOwnDraws},{"complete",complete},{"usable",e.validBase()},
            {"riichi",riichi},{"sh",sh},{"uke",uke},{"depth",depth}};
    }
};
struct RetentionDecision {
    vector<RetentionAlternative> alternatives;vector<retention::Entry> entries;
    int selected=-1;double unit=1000;string scope,resolution;
    RetentionDecision()=default;
    RetentionDecision(vector<RetentionAlternative> choices,string where):alternatives(move(choices)),unit(retentionUnit()),scope(move(where)),
        resolution(retentionBaselineMode?"complete_fast_model_baseline":"complete_full_model_pass"){
        for(const auto& x:alternatives)entries.push_back(x.entry());selected=retention::select(entries,unit);
    }
    bool valid()const{return selected>=0 && selected<int(alternatives.size()) && entries[selected].validBase();}
    const RetentionAlternative& chosen()const{return alternatives.at(selected);}
    int representative(int origin)const{
        if(valid() && chosen().origin==origin)return selected;
        int best=-1;
        for(int i=0;i<int(alternatives.size());++i)if(alternatives[i].origin==origin && entries[i].validBase())
            if(best<0 || retention::objective(entries[i],unit)>retention::objective(entries[best],unit))best=i;
        return best;
    }
    json explain()const{
        json rows=json::array();
        for(int i=0;i<int(alternatives.size());++i){auto row=alternatives[i].explain(unit);row["index"]=i;row["selected"]=i==selected;rows.push_back(move(row));}
        return {{"retention_decision",true},{"valuation_revision","continuous_win_and_deal_in_event_utility"},
            {"scope",scope},{"resolution",resolution},{"complete_candidate_pass",true},{"hard_filter",false},
            {"unit",unit},{"own_win_completion_reward",retention::WIN_UNITS*unit},{"deal_in_event_cost",retention::DEAL_IN_UNITS*unit},
            {"selection_rule","maximum continuous utility; explicit lower bound for unknown components"},
            {"action",valid()?json(chosen().action):json(nullptr)},{"selected_index",selected},{"candidates",rows}};
    }
};
inline thread_local RetentionDecision retainedDecision;
inline bool rememberRetention(RetentionDecision decision){
    if(!decision.valid())return false;
    retainedDecision=move(decision);const auto& a=retainedDecision.chosen();auto e=a.entry();deadlineResponse=a.action;
    deadlineSnapshot={{"source","last_complete_soft_pass"},{"scope",retainedDecision.scope},{"resolution",retainedDecision.resolution},
        {"off",a.off},{"base_score",e.base},{"score",retention::objective(e,retainedDecision.unit)},
        {"win",e.validWin()?json(a.conditionalWin):json(nullptr)},{"win_after_current_risk",e.validWin()?json(e.win):json(nullptr)},
        {"forecast_deal_in",e.validRisk()?json(e.dealIn):json(nullptr)},{"complete_candidate_set",true},{"hard_filter",false}};
    return true;
}
inline void traceRetentionFinal(const string& response){
    if(!debugPolicy)return;
    if(retainedDecision.valid() && retainedDecision.chosen().action==response){auto j=retainedDecision.explain();j["final_action"]=response;cerr<<j.dump()<<'\n';}
    else cerr<<json({{"retention_decision",true},{"final_action",response},{"status","terminal_or_no_comparable_alternative"},
        {"hard_filter",false},{"win_after_current_risk",response=="HU"?json(1.):json(nullptr)}}).dump()<<'\n';
}

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


// Included after State. The source owns historical maintenance of the bounds.
// This function is called only AFTER State::consume, once for every request.
inline void bindCurrentOpponentConstraints(State& state,const json& input){
    state.opponentConstraints=ohc::Snapshot::parse(input,state.seat,[]{return sharedSearchStopped();});
    array<int,34> visible=state.publicSeen,unknown{};bool consistent=true;
    for(int t:state.indicators)if(t>=0)++visible[norm(t)];
    for(int t=0;t<34;++t){visible[t]+=state.hand.c[t];consistent&=visible[t]>=0&&visible[t]<=4&&state.hand.c[t]>=0&&state.hand.c[t]<=4;
        unknown[t]=max(0,4-visible[t]);}
    array<int,4> capacities{};
    for(int p=0;p<4;++p)capacities[p]=13-3*int(state.pl[p].melds.size())+
        int(state.event=="DRAW"&&state.actor==p);
    consistent&=state.seat>=0&&state.seat<4;
    if(consistent)consistent=state.hand.n==capacities[state.seat];
    auto& snapshot=*state.opponentConstraints;
    snapshot.meta["engine_inventory_reference"]={{"self",state.seat},{"round_wind",state.round},
        {"opponent_concealed_capacities",capacities},{"own_hand_count",state.hand.n},
        {"unknown_total",accumulate(unknown.begin(),unknown.end(),0)},{"inventory_consistent",consistent}};
    snapshot.resolve(unknown,capacities,consistent);
}
inline void recordOpponentConstraintTelemetry(const State& state){
    if(!state.opponentConstraints)return;
    auto snapshot=state.opponentConstraints;
    dt::constraintInfo=snapshot->summary();
    if(debugPolicy)dt::constraintDetails=[snapshot]{auto out=snapshot->analysis();out["type"]="opponent_hand_constraints_analysis";return out;};
}


PositionValue positionOf(const State& st) {
    unsigned mask=0;
    for(int p=0;p<4;++p) if(st.pl[p].riichi) mask|=1u<<p;
    return PositionValue(st.seat,mask);
}
bool canDeclareRiichi(const State& st) {
    return !standing.enabled || positionOf(st).canDeclareRiichi();
}

#ifndef TF_PACKAGED_KOKUSHI_FILTER_HPP
#define TF_PACKAGED_KOKUSHI_FILTER_HPP

namespace infer {
using Counts=array<int,34>;
inline double currentConstraintDensity(ohc::Model* model,int who,const Counts& hand){
    if(!model)return 1.;try{return model->exactHandDensity(who,hand);}catch(const ohc::BudgetExpired&){throw ActionBudgetExpired{};}
}
struct Particle { Counts h{},left{}; double w=0; };
int cat(int t){return t>=31?3:t>=27?2:(t%9==0||t%9==8)?1:0;}
double choose(int n,int k) { if(k<0||k>n)return 0;double v=1;for(int i=1;i<=k;++i)v*=double(n-i+1)/i;return v; }
double uniform(mt19937_64& gen){return (gen()>>11)*0x1.0p-53;}
int draw(Counts& counts,bool outside,mt19937_64& gen) {
    int total=0;for(int t=0;t<34;++t)if(mj::terminal(t)==outside)total+=counts[t];
    if(!total)throw runtime_error("empty proposal category");
    int r=int(uniform(gen)*total);
    for(int t=0;t<34;++t)if(mj::terminal(t)==outside){if(r<counts[t]){--counts[t];return t;}r-=counts[t];}
    throw runtime_error("draw index");
}
// The observed river is already public. Guide INITIAL simple tiles toward
// it to preserve plausible rare trajectories, while correcting every draw by
// the exact physical/proposal density ratio. This changes variance, not the
// target initial-hand distribution or a risk threshold.
int drawGuided(Counts& counts,const Counts& observed,mt19937_64& gen,double& ratio) {
    double total=0,guided=0;for(int t=0;t<34;++t)if(!mj::terminal(t)){total+=counts[t];guided+=counts[t]*observed[t];}
    double u=uniform(gen);int selected=-1;double selectedPhysical=0,selectedProposal=0;
    for(int t=0;t<34;++t)if(!mj::terminal(t) && counts[t]) {
        double physical=counts[t]/total;
        double proposal=guided>0?.5*physical+.5*counts[t]*observed[t]/guided:physical;
        selected=t;selectedPhysical=physical;selectedProposal=proposal;
        if(u<proposal)break;u-=proposal;
    }
    if(selected<0)throw runtime_error("empty guided proposal");
    ratio*=selectedPhysical/selectedProposal;--counts[selected];return selected;
}
int kokushi(const Counts& c) {
    int unique=0,pair=0;for(int t=0;t<34;++t)if(mj::terminal(t)){unique+=c[t]>0;pair|=c[t]>=2;}
    return 13-unique-pair;
}
// Sufficient integer counts for exactly the same kokushi discard policy.
// All callers evaluate a 14-tile action, or add one physical tile to 13.
struct ActionSummary {
    int unique=0,pairs=0,plain=0,tripleTiles=0,doubles=0;
    ActionSummary(const Counts& c,int tiles) {
        int outside=0;
        for(int t:{0,8,9,17,18,26,27,28,29,30,31,32,33}) {
            int n=c[t];outside+=n;unique+=n>0;pairs+=n>=2;
            tripleTiles+=n>=3?n:0;doubles+=n==2;
        }
        plain=tiles-outside;
    }
    double probability(int held,bool outside) const {
        if(!held || unique==13 && pairs>0)return 0;
        if(outside && held<3 && !(held==2 && pairs>1))return 0;
        int total=plain+tripleTiles+(pairs>1?2*doubles:0);
        return double(held)/total;
    }
    double afterDraw(const Counts& c,int drawn,int discarded) const {
        ActionSummary next=*this;int n=c[drawn];
        if(!mj::terminal(drawn))++next.plain;
        else {
            next.unique+=n==0;next.pairs+=n==1;
            if(n==1)++next.doubles;
            else if(n==2){--next.doubles;next.tripleTiles+=3;}
            else if(n>=3)++next.tripleTiles;
        }
        return next.probability(c[discarded]+int(drawn==discarded),mj::terminal(discarded));
    }
};
double actionProbability(Counts& c,int discarded) {
    return ActionSummary(c,14).probability(c[discarded],mj::terminal(discarded));
}
vector<Particle> resample(const vector<Particle>& src,int n,mt19937_64& gen) {
    // Every occupied shanten stratum retains samples. Its probability mass is
    // unchanged; particles in a rare stratum have correspondingly small weight.
    // Ordinary equal-weight resampling had deleted legal rare ready branches.
    map<int,vector<const Particle*>> groups;map<int,double> mass;
    for(const auto& p:src)if(p.w>0){int key=kokushi(p.h);groups[key].push_back(&p);mass[key]+=p.w;}
    vector<Particle> out;out.reserve(n);int groupIndex=0;
    for(const auto& item:groups) {
        int count=n/int(groups.size())+(groupIndex++<n%int(groups.size()));
        double target=mass[item.first],at=uniform(gen)/count,cumulative=item.second[0]->w/target;size_t i=0;
        for(int k=0;k<count;++k,at+=1.0/count) {
            while(i+1<item.second.size() && at>cumulative)cumulative+=item.second[++i]->w/target;
            out.push_back(*item.second[i]);out.back().w=target/count;
        }
    }
    if((int)out.size()!=n)throw runtime_error("resampling count mismatch");
    return out;
}
struct ReadyBridge {
    double logReference=0,ready=0;array<double,34> ron{},doubleRon{};
    int patterns=0,trajectories=0,accepted=0;
};
ReadyBridge bridgeReady(const Counts& pool,const vector<int>& river,int samples,mt19937_64& gen,const array<bool,34>& safe,ohc::Model* constraintModel=nullptr,int constraintSeat=-1) {
    ReadyBridge out;Counts remaining=pool;int total=accumulate(pool.begin(),pool.end(),0);array<bool,34> furiten=safe;
    for(int d:river) {out.logReference+=log(double(remaining[d])/total);--remaining[d];--total;furiten[d]=true;}
    vector<int> terminals;for(int t=0;t<34;++t)if(mj::terminal(t))terminals.push_back(t);
    vector<pair<Counts,int>> shapes;Counts thirteen{};bool possible=true;
    for(int t:terminals){thirteen[t]=1;possible &= remaining[t]>=1;}
    if(possible)shapes.push_back({thirteen,-1});
    for(int missing:terminals)for(int pair:terminals)if(pair!=missing) {
        Counts shape=thirteen;shape[missing]=0;shape[pair]=2;possible=true;
        for(int t:terminals)possible &= shape[t]<=remaining[t];
        if(possible)shapes.push_back({shape,missing});
    }
    double denominator=choose(total,13);
    for(const auto& item:shapes) {
        double prior=1;for(int t:terminals)prior*=choose(remaining[t],item.first[t]);prior/=denominator;
        double sum=0;
        for(int trial=0;trial<samples;++trial) {
        checkActionDeadline();
            Counts h=item.first;double weight=1;
            for(int j=int(river.size())-1;j>=0;--j) {
                int d=river[j];++h[d];double likelihood=actionProbability(h,d);
                if(likelihood==0){weight=0;break;}
                // Half physical reverse draw, half proposing the observed
                // discard as the latent drawn tile. Correct the proposal.
                double u=uniform(gen);int selected=-1;double physical=0,proposal=0;
                for(int t=0;t<34;++t)if(h[t]) {
                    double p=double(h[t])/14,q=.5*p+.5*(t==d);
                    selected=t;physical=p;proposal=q;if(u<q)break;u-=q;
                }
                if(selected<0)throw runtime_error("backward draw index");
                weight*=14*likelihood*physical/proposal;--h[selected];
            }
            ++out.trajectories;
            if(weight>0) {
                mj::Hand hand;int pairs=0,kinds=0;
                for(int t=0;t<34;++t){if(h[t])hand.add(t,h[t]);pairs+=h[t]>=2;kinds+=h[t]>0;}
                int ks=kokushi(h),normal=mj::standardShanten(hand,0),seven=6-pairs+max(0,7-kinds);
                if(ks<=min(normal,seven)){weight/=1+(ks==normal)+(ks==seven);sum+=weight;++out.accepted;}
            }
        }
        double mass=prior*sum/samples;if(constraintModel)mass*=currentConstraintDensity(constraintModel,constraintSeat,item.first);out.ready+=mass;++out.patterns;
        if(item.second>=0){if(!furiten[item.second])out.ron[item.second]+=mass;}
        else {
            bool blocked=false;for(int t:terminals)blocked|=furiten[t];
            if(!blocked)for(int t:terminals){out.ron[t]+=mass;out.doubleRon[t]+=mass;}
        }
    }
    return out;
}
json run(const json& q,ohc::Model* constraintModel=nullptr,int constraintSeat=-1) {
    Counts pool=q.at("pool").get<Counts>();vector<int> river;
    for(auto t:q.at("river"))river.push_back(mj::norm(mj::tile(t.get<string>())));
    int inside=0,outside=0;for(int t=0;t<34;++t){if(pool[t]<0||pool[t]>4)throw runtime_error("invalid pool");(mj::terminal(t)?outside:inside)+=pool[t];}
    int total=inside+outside;if(total<13+(int)river.size())throw runtime_error("public pool too small for history");
    Counts observed{};for(int t:river)++observed[t];
    int per=q.value("per_stratum",512),n=per*14;mt19937_64 gen(q.value("seed",uint64_t(20261020)));
    vector<Particle> particles;double priorK=0;
    for(int m=0;m<=13;++m) {
        checkActionDeadline();
        if(m>outside || 13-m>inside)continue;
        double mass=choose(outside,m)*choose(inside,13-m)/choose(total,13)/per;
        for(int k=0;k<per;++k) {
            Particle p;p.left=pool;mj::Hand hand;double correction=1;
            for(int i=0;i<13;++i){int t=i<m?draw(p.left,true,gen):drawGuided(p.left,observed,gen,correction);++p.h[t];hand.add(t);}
            int ks=kokushi(p.h),normal=mj::standardShanten(hand,0),pairs=0,kinds=0;
            for(int t=0;t<34;++t){pairs+=p.h[t]>=2;kinds+=p.h[t]>0;}
            int seven=6-pairs+max(0,7-kinds);
            if(ks>min(normal,seven))continue;
            int tied=1+(ks==normal)+(ks==seven);p.w=mass*correction/tied;priorK+=p.w;particles.push_back(p);
        }
    }
    double sampledMass=priorK;bool noInitial=sampledMass<=0;priorK=clamp(sampledMass,0.,1.);
    if(!noInitial){for(auto& p:particles)p.w/=sampledMass;particles=resample(particles,n,gen);}
    double logK=noInitial?-1e300:log(priorK),logNormal=priorK<1?log1p(-priorK):-1e300;Counts marginal=pool;
    auto emissions=q.at("ordinary_emissions");json trace=json::array();bool exhausted=noInitial;
    for(int step=0;step<(int)river.size();++step) {
        checkActionDeadline();
        int d=river[step];double sum=0,sumSquares=0;
        array<int,4> avail{};for(int t=0;t<34;++t)avail[cat(t)]+=marginal[t];
        auto probabilities=emissions[min(24,step+1)].get<array<double,4>>();double normalizer=0;
        for(int c=0;c<4;++c)if(avail[c])normalizer+=probabilities[c];
        if(!marginal[d])throw runtime_error("observed discard absent from physical pool");
        double pd=probabilities[cat(d)]/normalizer*marginal[d]/avail[cat(d)];logNormal+=log(pd);--marginal[d];
        if(exhausted)continue;
        for(auto& p:particles) {
            array<double,34> drawWeight{};double likelihood=0;int left=accumulate(p.left.begin(),p.left.end(),0);
            ActionSummary summary(p.h,13);
            for(int t=0;t<34;++t)if(p.left[t]) {
                double probability=summary.afterDraw(p.h,t,d);
                drawWeight[t]=double(p.left[t])/left*probability;likelihood+=drawWeight[t];
            }
            p.w*=likelihood;sum+=p.w;sumSquares+=p.w*p.w;
            if(likelihood>0) {
                double u=uniform(gen)*likelihood;int selected=-1;
                for(int t=0;t<34;++t)if(drawWeight[t]>0){selected=t;if(u<drawWeight[t])break;u-=drawWeight[t];}
                if(selected<0)throw runtime_error("conditional draw index");
                --p.left[selected];++p.h[selected];--p.h[d];
            }
        }
        if(sum<=0){exhausted=true;particles.clear();continue;}
        logK+=log(sum);double effective=sum*sum/sumSquares;for(auto& p:particles)p.w/=sum;
        if(q.value("record_trace",false))trace.push_back({{"turn",step+1},{"log_likelihood_k",logK},{"log_likelihood_ordinary",logNormal},
            {"posterior_pursuit",1/(1+exp(clamp(logNormal-logK,-700.,700.)))},{"effective_particles",effective}});
        particles=resample(particles,n,gen);
    }
    if(constraintModel && !exhausted){
        double mass=0;for(auto& particle:particles){particle.w*=currentConstraintDensity(constraintModel,constraintSeat,particle.h);mass+=particle.w;}
        if(mass>0){logK+=log(mass);for(auto& particle:particles)particle.w/=mass;}
        else{exhausted=true;logK=-1e300;particles.clear();}
    }
    double pursuit=exhausted?0:1/(1+exp(clamp(logNormal-logK,-700.,700.)));
    array<double,34> ron{},doubleRon{};double ready=0;array<double,14> shanten{};array<bool,34> discarded{};
    for(int t:river)discarded[t]=true;
    if(!exhausted)for(const auto& p:particles) {
        int ks=kokushi(p.h);shanten[clamp(ks,0,13)]+=p.w;
        if(ks!=0)continue;ready+=p.w;
        int missing=-1;for(int t=0;t<34;++t)if(mj::terminal(t) && !p.h[t])missing=t;
        if(missing>=0){if(!discarded[missing])ron[missing]+=p.w;}
        else {
            bool furiten=false;for(int t=0;t<34;++t)if(mj::terminal(t))furiten|=discarded[t];
            if(!furiten)for(int t=0;t<34;++t)if(mj::terminal(t)){ron[t]+=p.w;doubleRon[t]+=p.w;}
        }
    }
    array<bool,34> safe=q.value("safe",array<bool,34>{});
    auto bridge=bridgeReady(pool,river,q.value("bridge_samples",256),gen,safe,constraintModel,constraintSeat);
    double logReady=bridge.ready>0?bridge.logReference+log(bridge.ready):-1e300;
    double logNotReady=(!exhausted && ready<1)?logK+log1p(-ready):-1e300;
    double common=max({logReady,logNotReady,logNormal});
    double zReady=exp(logReady-common),zOther=exp(logNotReady-common),zNormal=exp(logNormal-common),z=zReady+zOther+zNormal;
    double forwardReady=ready,forwardPursuit=pursuit;
    pursuit=clamp((zReady+zOther)/z,0.,1.);ready=clamp(zReady/z,0.,pursuit);
    for(int t=0;t<34;++t){ron[t]=bridge.ready>0?ready*bridge.ron[t]/bridge.ready:0;doubleRon[t]=bridge.ready>0?ready*bridge.doubleRon[t]/bridge.ready:0;}
    if(pursuit>0) {
        for(int k=1;k<14;++k)shanten[k]=forwardReady<1?shanten[k]/(1-forwardReady)*(zOther/z)/pursuit:0;
        shanten[0]=ready/pursuit;
    }
    // Honors absent from the target river, with equal physical capacity and
    // equal pass/furiten status, are exchangeable in this shanten-only latent
    // model. Average their estimates instead of allowing Monte Carlo labels
    // to create a preference between mathematically identical outcomes.
    // Preserve total probability mass; no empirical risk cutoff is introduced.
    for(int capacity=0;capacity<=4;++capacity)for(int blocked=0;blocked<2;++blocked) {
        vector<int> group;double first=0,second=0;
        for(int t=27;t<34;++t)if(pool[t]==capacity && !observed[t] && int(safe[t])==blocked) {
            group.push_back(t);first+=ron[t];second+=doubleRon[t];
        }
        if(!constraintModel){if(group.size()>1)for(int t:group){ron[t]=first/group.size();doubleRon[t]=second/group.size();}}
        else{vector<vector<int>> subgroups;for(int t:group){bool found=false;for(auto& g:subgroups)if(constraintModel->exchangeable(t,g[0])){g.push_back(t);found=true;break;}if(!found)subgroups.push_back({t});}
            for(const auto& g:subgroups)if(g.size()>1){double a=0,b=0;for(int t:g){a+=ron[t];b+=doubleRon[t];}for(int t:g){ron[t]=a/g.size();doubleRon[t]=b/g.size();}}}
    }
    return json({{"prior_pursuit",priorK},{"posterior_pursuit",pursuit},{"ready_probability",ready},
        {"ron_probability",ron},{"double_ron_probability",doubleRon},{"conditional_shanten",shanten},
        {"particles",n},{"particle_collapse",exhausted},{"trace",trace},
        {"log_evidence",common+log(z)},{"normal_log_evidence",logNormal},
        {"forward_ready_probability",forwardPursuit*forwardReady},{"bridge_patterns",bridge.patterns},
        {"bridge_trajectories",bridge.trajectories},{"bridge_accepted",bridge.accepted}});
}
}

#endif


#ifndef TF_PACKAGED_KOKUSHI_RISK_HPP
#define TF_PACKAGED_KOKUSHI_RISK_HPP

namespace infer {
struct RiskPosterior {
    double ready=0,pursuit=0;bool structurallyImpossible=false;
    array<double,34> ron{},doubleRon{};
    array<double,3> seedReady{};
};
inline thread_local unordered_map<string,RiskPosterior> actionCache;
inline RiskPosterior predict(const array<int,34>& unseen,const Player& player,ohc::Model* constraintModel=nullptr,int constraintSeat=-1) {
    int missing=0;bool pairAvailable=false;
    for(int t=0;t<34;++t)if(terminal(t)){missing+=unseen[t]==0;pairAvailable|=unseen[t]>=2;}
    if(missing>=2 || (missing==1 && !pairAvailable)) {
        RiskPosterior impossible;impossible.structurallyImpossible=true;return impossible;
    }
    array<int,34> pool=unseen;vector<string> river;
    for(auto d:player.river){++pool[norm(d.raw)];river.push_back(name(d.raw));}
    json base={{"pool",pool},{"river",river},{"per_stratum",retentionBaselineMode?16:512},{"bridge_samples",retentionBaselineMode?8:256}};
    if(constraintModel){base["current_constraint_key"]=constraintModel->signature;base["constraint_absolute_seat"]=constraintSeat;}
    string key=base.dump();auto found=actionCache.find(key);
    if(found==actionCache.end()) {
        static const json emissions=json::parse(R"EMISSIONS([[0.14166536389863987,0.2134040617938408,0.4850454073874047,0.15988516692011454],[0.16516613816829318,0.22341255274138547,0.4334796614139936,0.17794164767632775],[0.20475512772589946,0.240007407382005,0.35354542679894746,0.2016920380931481],[0.2579051741934678,0.25186892793914345,0.2799938692907766,0.21023202857661208],[0.32596074615984455,0.2602044831243869,0.2089334711208719,0.20490129959489678],[0.383049073576891,0.26040270473738963,0.17378374944152253,0.1827644722441969],[0.4279470356778412,0.25852205318267785,0.15413427119468123,0.15939663994479966],[0.45966408045726764,0.2607410562787563,0.13994351458968882,0.13965134867428722],[0.48729277306912255,0.2596457230222107,0.13530912566449954,0.11775237824416723],[0.5109175154102228,0.2601239558818568,0.13215517087552253,0.0968033578323978],[0.5252015728641225,0.2579576597688363,0.13340066614086546,0.08344010122617578],[0.5330534166290637,0.2574232401439192,0.13058339210878378,0.07893995111823326],[0.5335496371694497,0.251169330359811,0.13445333215902272,0.08082770031171661],[0.5405921754423053,0.23951103721998532,0.13967729225328435,0.08021949508442491],[0.5551387774670008,0.22473011811924687,0.13772468073112037,0.08240642368263196],[0.5756853992992063,0.20974734545500018,0.12858406554077076,0.08598318970502278],[0.5935975428682939,0.2032478937052969,0.11314175172448901,0.09001281170192005],[0.5956612882734088,0.20801677042307204,0.10862025155634608,0.08770168974717316],[0.58642378144831,0.22479634547009322,0.11014222062199018,0.07863765245960663],[0.5763528341272814,0.24578493817909333,0.11320451747578013,0.0646577102178451],[0.5718466491074042,0.250804799531753,0.11745195590674079,0.05989659545410203],[0.6261859582542694,0.2941176470588235,0.04554079696394687,0.03415559772296015],[0.6176470588235294,0.17647058823529413,0.11764705882352941,0.08823529411764706],[0.6176470588235294,0.17647058823529413,0.11764705882352941,0.08823529411764706],[0.6176470588235294,0.17647058823529413,0.11764705882352941,0.08823529411764706]])EMISSIONS");
        base["ordinary_emissions"]=emissions;
        array<uint64_t,3> seeds{{17,43,109}};array<json,3> results;array<double,3> evidence{};
        double common=-1e300;
        int seedCount=retentionBaselineMode?1:3;
        for(int i=0;i<seedCount;++i){checkActionDeadline();base["seed"]=seeds[i];results[i]=run(base,constraintModel,constraintSeat);evidence[i]=results[i].at("log_evidence");common=max(common,evidence[i]);}
        double total=0;RiskPosterior result;
        for(int i=0;i<seedCount;++i) {
            double w=exp(evidence[i]-common);total+=w;result.seedReady[i]=results[i].at("ready_probability");
            result.ready+=w*result.seedReady[i];result.pursuit+=w*results[i].at("posterior_pursuit").get<double>();
            for(int t=0;t<34;++t){result.ron[t]+=w*results[i]["ron_probability"][t].get<double>();result.doubleRon[t]+=w*results[i]["double_ron_probability"][t].get<double>();}
        }
        result.ready/=total;result.pursuit/=total;
        for(int t=0;t<34;++t){result.ron[t]/=total;result.doubleRon[t]/=total;}
        found=actionCache.emplace(key,result).first;
    }
    // Waiting sets, rather than independent tile guesses, determine furiten.
    auto result=found->second;bool thirteenBlocked=false;
    for(int t=0;t<34;++t)if(terminal(t) && player.safe[t])thirteenBlocked=true;
    for(int t=0;t<34;++t) {
        double single=max(0.,result.ron[t]-result.doubleRon[t]);
        result.doubleRon[t]=thirteenBlocked?0:result.doubleRon[t];
        result.ron[t]=(player.safe[t]?0:single)+result.doubleRon[t];
    }
    return result;
}
}

#endif


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


// Included after Context, inside namespace mj.
inline void Context::buildFutureExposure() {
    if(!standing.enabled)return;
    for(int elapsed=0;elapsed<TemporalOtherWins::MAX_TIME;++elapsed) {
        auto projected=otherWins.project(elapsed);
        const auto& weights=declarationWeights[elapsed];
        for(auto& x:futureDrawExposure[elapsed]){x.survival=0;x.loss=0;}
        if(totalUnknown<=0) {for(auto& x:futureDrawExposure[elapsed])x.survival=1;continue;}
        for(int tile=0;tile<34;++tile)if(unseen[tile]>0) {
            double tileWeight=constraintModel?constraintWallProbability[tile]:double(unseen[tile])/totalUnknown;
            for(unsigned mask:declarations.masks)if(weights[mask]>0) {
                double survive=1;array<double,2> loss{};
                for(int who=0;who<4;++who)if(who!=st.seat) {
                    double ready=mask&(1u<<who)?1:projected.silent[who];
                    double ordinary=ronShape[who][tile]*ready;
                    double twice=specialDoubleRon[who][tile];
                    double single=max(0.,specialRon[who][tile]-twice);
                    double probability=clamp(ordinary+single+twice,0.,1.);
                    survive*=1-probability;
                    for(int committed=0;committed<2;++committed) {
                        // Payout and risk use the SAME projected declaration
                        // mask, including accepted sticks and actual point
                        // thresholds. The self-payer column was unused by
                        // other-player stopping events (actor==self returns).
                        double gain=ordinary*otherWins.ronValue[mask][who][st.seat][committed]+
                            single*specialSingleValue[mask][who][committed]+
                            twice*specialDoubleValue[mask][who][committed];
                        loss[committed]-=cfg.risk*gain;
                    }
                }
                double weight=tileWeight*weights[mask];
                for(int committed=0;committed<2;++committed) {
                    futureDrawExposure[elapsed][committed].survival+=weight*survive;
                    // Keep the existing nonnegative coarse future-loss
                    // convention. Immediate discard values remain signed.
                    futureDrawExposure[elapsed][committed].loss+=weight*max(0.,loss[committed]);
                }
            }
        }
        for(auto& x:futureDrawExposure[elapsed])x.survival=clamp(x.survival,0.,1.);
    }
    if(debugPolicy) {
        json times=json::array();
        for(int elapsed:{0,4,12,24,40})if(elapsed<=st.remaining) {
            const auto& f=futureDrawExposure[elapsed];auto p=otherWins.project(elapsed);
            times.push_back({{"elapsed",elapsed},{"survival",f[0].survival},
                {"loss_dama",f[0].loss},{"loss_committed",f[1].loss},
                {"locked",p.locked},{"silent_ready",p.silent}});
        }
        cerr<<json({{"future_discard_model",true},{"current_mean_danger",meanDanger},
            {"source","existing public progress/declaration parameters; no risk refit"},{"times",times}}).dump()<<'\n';
    }
}


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


// Included inside namespace mj, after Hand/Meld/Future are defined.
struct FutureCallShape {
    int tile=-1,a=-1,b=-1,discard=-1;
    bool pon=false;
    Hand after;
};
inline vector<FutureCallShape> futureCallShapes(const Hand& h,int tile,int melds) {
    vector<FutureCallShape> out;
    int sh=shanten(h,melds);
    if(sh<=0 || melds>=4)return out;
    vector<pair<int,int>> pairs;
    if(h.c[tile]>=2)pairs.push_back({tile,tile});
    if(tile<27)for(int begin=max(tile/9*9,tile-2);begin<=min(tile,tile/9*9+6);++begin) {
        vector<int> need;
        for(int t=begin;t<begin+3;++t)if(t!=tile)need.push_back(t);
        if(h.c[need[0]] && h.c[need[1]])pairs.push_back({need[0],need[1]});
    }
    for(auto [a,b]:pairs) {
        Hand next=h;next.add(a,-1);next.add(b,-1);
        int begin=min({a,b,tile});bool pon=a==b;
        for(int d=0;d<34;++d)if(next.c[d]) {
            if(d==tile || (!pon && d/9==tile/9 && ((tile==begin && d==tile+3) || (tile==begin+2 && d==tile-3))))continue;
            next.add(d,-1);
            if(shanten(next,melds+1)==sh-1)out.push_back({tile,a,b,d,pon,next});
            next.add(d);
        }
    }
    return out;
}
struct CallContinuation {
    static constexpr int WALL=76;
    int tile=-1;
    bool pon=false;
    double copies=0;
    array<double,WALL> value{},win{},dealIn{};
    CallContinuation(){value.fill(-1e30);}
};


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

// Included inside Evaluator. All conditioning is against the current snapshot;
// futureWallDraws records only hypothetical samples from the initial wall.
Hand futureWallDraws;
mutable ohc::Counts constraintLastAcquired{},constraintLastWall{};
mutable bool constraintViewValid=false;
mutable ohc::Distribution constraintView;
const ohc::Distribution& currentConstraintView()const{
    if(!cx.constraintModel)throw logic_error("constraint view without current model");
    if(!constraintViewValid || constraintLastAcquired!=futureDraws.c || constraintLastWall!=futureWallDraws.c){
        array<bool,4> locked{};for(int p=0;p<4;++p)locked[p]=cx.st.pl[p].riichi;
        try{constraintView=cx.constraintModel->distribution(futureDraws.c,futureWallDraws.c,locked);}
        catch(const ohc::BudgetExpired&){throw ActionBudgetExpired{};}
        if(!constraintView.possible)throw logic_error("zero-support hypothetical wall prefix");
        constraintLastAcquired=futureDraws.c;constraintLastWall=futureWallDraws.c;constraintViewValid=true;
    }
    return constraintView;
}
double conditionedDrawRate(int t)const{
    const auto& view=currentConstraintView();
    // Preserve the parent's effective-pool/horizon convention. Conditioning
    // changes a tile's relative source probability, not visible tile counts.
    return view.wall[t]*view.remainingUnknown/pool;
}
array<double,4> conditionedReleaseRatios(int t)const{
    const auto& view=currentConstraintView();array<double,4> ratios{};
    int copies=max(0,cx.unseen[t]-futureDraws.c[t]);
    if(!copies || !view.remainingUnknown)return ratios;
    double prior=double(copies)/view.remainingUnknown;
    for(int p=0;p<4;++p)if(p!=cx.st.seat)ratios[p]=view.release[p][t]/prior;
    return ratios;
}
double conditionedGenericRate(const Hand& h,int t,int copies)const{
    double rate=conditionedDrawRate(t),extra=0;bool fromAll=true;
    if(cx.st.closed())return rate;
    if(!cfg.call)extra=.13;
    else{bool pon=false,chi=false;for(const auto& x:futureCallShapes(h,t,cx.melds)){pon|=x.pon;chi|=!x.pon;}
        if(pon)extra=cfg.pon-1;else if(chi){extra=cfg.chi-1;fromAll=false;}}
    if(extra<=0)return rate;auto release=conditionedReleaseRatios(t);double factor=0;
    if(fromAll){for(int p=0;p<4;++p)if(p!=cx.st.seat)factor+=release[p]/3.;}
    else factor=release[(cx.st.seat+3)%4];
    return rate+copies/pool*extra*factor;
}

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


// Included inside namespace mj, after the complete Evaluator definition.
inline vector<CallContinuation> Evaluator::callContinuations(const Hand& h,int sh,int depth) {
    vector<CallContinuation> out;
    vector<array<retention::Frontier,CallContinuation::WALL>> alternatives;
    if(!standing.enabled || !cfg.call || cx.st.closed() || cx.st.remaining<=1 || cx.melds>=4 || exhausted)return out;
    struct TraceGuard {bool saved=debugPolicy;TraceGuard(){debugPolicy=false;}~TraceGuard(){debugPolicy=saved;}} quiet;
    for(int t=0;t<34;++t)if(available(h,t)>0 && h.c[t]<4) {
        auto options=futureCallShapes(h,t,cx.melds);
        for(const auto& option:options) {
            if( dt::timeLimit(chrono::duration<double>(chrono::steady_clock::now()-started).count()>SEARCH_SECONDS)) {
                exhausted=true;return {};
            }
            auto found=find_if(out.begin(),out.end(),[&](const auto& row){return row.tile==t && row.pon==option.pon;});
            if(found==out.end()) {
                out.emplace_back();alternatives.emplace_back();found=prev(out.end());found->tile=t;found->pon=option.pon;found->copies=available(h,t);
            }
            int group=int(found-out.begin());
            State next=cx.st;next.hand=h;next.raw.fill(0);
            for(int x=0;x<34;++x)next.raw[x]=h.c[x];
            for(int suit=0;suit<3;++suit) {
                int red=34+suit,normal=9*suit+4;
                if(cx.st.raw[red] && h.c[normal] && rootOut!=red) {--next.raw[normal];next.raw[red]=1;}
            }
            auto consume=[&](int x) {
                int raw=x;
                if(x<27 && x%9==4 && next.raw[34+x/9])raw=34+x/9;
                next.add(raw,-1);++next.publicSeen[x];return raw;
            };
            int a=consume(option.a),b=consume(option.b);
            Meld meld;meld.tiles={t,a,b};meld.kind=option.pon?1:0;meld.called=t;
            next.pl[next.seat].melds.push_back(meld);++next.publicSeen[t];
            int discard=option.discard;
            // Retain a known red when an ordinary copy can be discarded.
            int raw=next.raw[discard]?discard:34+discard/9;
            next.add(raw,-1);++next.publicSeen[discard];
            next.actor=next.seat;next.replacementForecast=false;
            if(next.hand.c!=option.after.c)throw runtime_error("future call hand mismatch");
            auto exposure=futureExposure(discard); // before the proposed tile passes
            // Four residues represent the actual number of live tiles left
            // after a call. Calling consumes no draw and resets the next actor
            // to our shimocha; it does not grant the replaced own normal draw.
            // The four clocks use the same public inference and declared
            // payoff cache. Copy once, retaining the immutable parent.
            Context childContext(next,cx);
            // Like the ordinary Future array's many horizons, these are four
            // temporal evaluations of one hand node, not four extra hands.
            enterNode();dt::futureCall();
            for(int residue=0;residue<4;++residue) {
                int maximum=cx.st.remaining-1;
                maximum-=(maximum-residue+4)%4;
                if(maximum<=0)continue;
                next.remaining=maximum;
                Evaluator child(childContext,timeOffset+cx.st.remaining-maximum,this);
                child.abortable=abortable;child.rootOut=rootOut;child.pool=pool;
                child.futureDraws=futureDraws;child.futureWallDraws=futureWallDraws;child.futureDraws.add(t);
                child.futureDiscards=futureDiscards|(U64(1)<<discard);
                auto concealed=option.after;
                // Expand one future call on a path. A ready child has concrete
                // waits and scores; a distant child retains its locked meld
                // in the legal shape estimate. Avoid multiplying the own-draw
                // tree by four residue trees at every additional future call.
                auto value=child.eval(concealed,sh-1,0);
                if(child.exhausted ||  dt::timeLimit(chrono::duration<double>(chrono::steady_clock::now()-started).count()>SEARCH_SECONDS)) {
                    exhausted=true;return {};
                }
                if(child.nodes!=1)throw runtime_error("future-call child must be a single leaf");
                for(int remaining=residue?residue:4;remaining<=maximum;remaining+=4) {
                    int k=remaining/4;
                    double priced=exposure.value(value.v[k]);
                    alternatives[group][remaining].addPolicy(priced,exposure.win(value.win[k]),exposure.dealIn(value.dealIn[k]),discard);
                }
            }
        }
    }
    for(int group=0;group<int(out.size());++group)for(int left=1;left<CallContinuation::WALL;++left){
        auto& options=alternatives[group][left];if(options.entries.empty())continue;
        int chosen=options.selected(retentionUnit());if(chosen<0){exhausted=true;return {};}
        out[group].value[left]=options.entries[chosen].base;out[group].win[left]=options.entries[chosen].win;out[group].dealIn[left]=options.entries[chosen].dealIn;
    }
    return out;
}

inline retention::Value Evaluator::advanceCalls(const vector<CallContinuation>& calls,int after,int draws,
        int remaining,int elapsed,double continuation,double win,double risk) const {
    for(int step=draws;step>=1;--step) {
        int source=(after+step)%4,left=remaining-step;
        if(source==cx.st.seat)throw runtime_error("call prefix contains own draw");
        // No call is legal after the last normal tile. Other calls are optional
        // and compete for the same offered tile, including chi/pon overlap.
        if(left>0 && left<CallContinuation::WALL) {
            array<double,34> gain{},winDelta{},riskDelta{},mass{};
            array<retention::Frontier,34> alternatives;array<vector<double>,34> offerMass;
            for(int i=0;i<int(calls.size());++i){const auto& branch=calls[i];
                if(!branch.pon && (cx.st.seat-source+4)%4!=1)continue;
                if(branch.value[left]<=-1e19)continue;
                double offer=branch.copies/pool*(branch.pon?max(0.,cfg.pon-1)/3:max(0.,cfg.chi-1));
                if(cx.constraintModel)offer*=conditionedReleaseRatios(branch.tile)[source];
                if(offer<=0)continue;auto& options=alternatives[branch.tile];auto& weights=offerMass[branch.tile];
                if(options.entries.empty()){options.addPolicy(continuation,win,risk,-1);weights.push_back(0);}
                options.addPolicy(branch.value[left],branch.win[left],branch.dealIn[left],i);weights.push_back(offer);
            }
            for(int tile=0;tile<34;++tile){auto& options=alternatives[tile];if(options.entries.empty())continue;
                int best=-1;double bestGain=-1e30;
                for(int i=0;i<int(options.entries.size());++i)if(options.entries[i].validBase()){
                    double candidate=offerMass[tile][i]*(retention::objective(options.entries[i],retentionUnit())-retention::objective({continuation,win,true,true,risk},retentionUnit()));
                    if(best<0 || candidate>bestGain){best=i;bestGain=candidate;}
                }
                if(best>0){double offer=offerMass[tile][best];mass[tile]=offer;
                    gain[tile]=offer*(options.entries[best].base-continuation);winDelta[tile]=offer*(options.entries[best].win-win);riskDelta[tile]=offer*(options.entries[best].dealIn-risk);}
            }
            double total=accumulate(mass.begin(),mass.end(),0.);
            double scale=total>.85?.85/total:1;
            continuation+=scale*accumulate(gain.begin(),gain.end(),0.);
            win+=scale*accumulate(winDelta.begin(),winDelta.end(),0.);risk+=scale*accumulate(riskDelta.begin(),riskDelta.end(),0.);
        }
        auto event=otherInterval((source+3)%4,1,pressuredSurvival(false),elapsed+step-1);
        continuation=event.stopped[0]+event.survival*continuation;
        win*=event.survival;risk*=event.survival;
    }
    return {continuation,win,risk};
}


// Included inside namespace mj, after Evaluator.
struct ParallelStageUnavailable {};
struct ParallelSetupTimer {size_t worker;chrono::steady_clock::time_point start=chrono::steady_clock::now();~ParallelSetupTimer(){prt::runtime.addSetup(worker,prt::seconds(start,chrono::steady_clock::now()));}};
struct ParallelSettings {
    chrono::steady_clock::time_point setupBegan=chrono::steady_clock::now();
    StandingContext match;Parameters parameters;chrono::steady_clock::time_point start;
    vector<unsigned char> initialized;
    bool baseline=retentionBaselineMode;string validActions=deadlineValidActions;
    ParallelSettings():match(standing),parameters(cfg),start(actionStarted),initialized(prt::runtime.count(),0){initialized[0]=1;dt::mainSnapshotSeconds+=prt::seconds(setupBegan,chrono::steady_clock::now());}
    void enter(size_t worker){if(initialized[worker])return;
        dt::beginWorker(prt::request.sequence,start,SEARCH_SECONDS);ParallelSetupTimer timer{worker};
        actionStarted=start;enforceActionDeadline=true;debugPolicy=false;retentionBaselineMode=baseline;deadlineValidActions=validActions;cfg=parameters;standing=match;infer::actionCache.clear();initialized[worker]=1;
    }
};
template<class Candidate,class Work>
bool parallelBatch(vector<Candidate>& committed,const vector<int>& selected,const char* kind,int progress,int shape,Work work) {
    if(selected.empty())return true;
    if(sharedSearchStopped())return false;
    std::unordered_set<int> unique;
    for(int i:selected){if(!unique.insert(i).second){++dt::duplicateJobs;throw runtime_error("duplicate candidate in parallel batch");}
        uint64_t key=(uint64_t(kind[0]=='d'?1:2)<<48)|(uint64_t(i)<<16)|(unsigned(progress)<<8)|unsigned(shape);
        if(!dt::visitedRootJobs.insert(key).second)++dt::rootJobRevisits;
    }
    vector<optional<Candidate>> results(committed.size());vector<string> errors(committed.size());
    auto begin=chrono::steady_clock::now();prt::request.tasksScheduled+=unsigned(selected.size());
    prt::runtime.batch([&](size_t worker){
        for(int i:selected)if(size_t(i)%prt::runtime.count()==worker) {
            if(sharedSearchStopped())break;
            try {results[i]=work(worker,i);}
            catch(const ParallelStageUnavailable&){break;}
            catch(const ActionBudgetExpired&){dt::interrupted();prt::request.cancelled=true;break;}
            catch(const exception& e){errors[i]=e.what();dt::interrupted();prt::request.cancelled=true;break;}
        }
        dt::saveWorker(worker);
    });
    int done=0;for(int i:selected)done+=results[i].has_value();
    prt::request.tasksCompleted+=done;bool complete=done==int(selected.size());
    if(complete)for(int i:selected)committed[i]=move(*results[i]);
    else ++prt::request.incompleteBatches;
    dt::layers.push_back({kind,progress,shape,int(selected.size()),done,complete,prt::seconds(begin,chrono::steady_clock::now())});
    for(int i:selected)if(!errors[i].empty()){dt::r.error=errors[i];cerr<<json({{"type","parallel_search_error"},{"candidate",i},{"error",errors[i]}}).dump()<<'\n';}
    return complete;
}
inline void resetHypotheticalState(Evaluator& ev){ev.futureDraws=Hand{};ev.futureWallDraws=Hand{};ev.constraintViewValid=false;ev.futureDiscards=0;}


// Included inside namespace mj after WinScore and Evaluator definitions.
inline void OrderedWinInterval::addTsumo(double probability,const WinScore& score) {
    if(!ownDraw || score.tsumo<=0 || probability<=0)return;
    tsumo.probability+=probability;
    tsumo.gain+=probability*payouts->average(score.futureUtility[4],ownWeights);
}
inline void OrderedWinInterval::addRon(double probability,const WinScore& ordinary,const WinScore& last,bool lastOrdinary,const array<double,4>* release) {
    if(probability<=0)return;
    if(ordinary.ron>0)for(int payer=0;payer<4;++payer)if(extraSources[payer]>0) {
        double p=probability*extraSources[payer];if(release)p*=(*release)[payer];extra.probability+=p;
        extra.gain+=p*payouts->average(ordinary.futureUtility[payer],extraWeights);
    }
    for(int i=0;i<opponents;++i) {
        const auto& score=lastOrdinary && i==opponents-1?last:ordinary;
        if(score.ron<=0)continue;
        int payer=actors[i];double p=probability*normalSources[payer];if(release)p*=(*release)[payer];
        ron[i].probability+=p;
        // These weights already contain survival of THIS player's tsumo,
        // jointly with the same declaration mask used for our payment.
        ron[i].gain+=p*payouts->average(score.futureUtility[payer],noTsumoWeights[i]);
    }
}
inline OrderedWinInterval Evaluator::buildWinInterval(int after,int opponentDraws,int elapsed,bool committed,bool ownDraw,int ownElapsed) {
    OrderedWinInterval out;out.payouts=&cx.declarations;out.opponents=opponentDraws;
    out.elapsed=elapsed;out.ownElapsed=ownElapsed;out.ownDraw=ownDraw;out.committed=committed;
    if(after<0 || after>=4)after=cx.st.seat;
    if(opponentDraws<0 || opponentDraws>3)throw runtime_error("invalid ready interval length");
    out.extraWeights=cx.projectedDeclarations(elapsed+timeOffset);
    out.ownWeights=cx.projectedDeclarations(ownElapsed+timeOffset);
    double total=0;int canCall=0;
    for(int p=0;p<4;++p)if(p!=cx.st.seat && p!=after && !cx.st.pl[p].riichi)++canCall;
    double callShare=canCall && opponentDraws>0?EXTRA_CALL_SOURCE_SHARE:0;
    for(int i=0;i<opponentDraws;++i) {
        int actor=(after+i+1)%4;if(actor==cx.st.seat)throw runtime_error("ready interval contains an earlier own draw");
        out.actors[i]=actor;
        out.normalSources[actor]=cx.st.pl[actor].riichi?LOCKED_RIICHI_SOURCE_MULTIPLIER:1.;
        total+=out.normalSources[actor];
        int time=elapsed+i+timeOffset;
        out.other[i]=cx.otherWins.orderedStep(actor,time,pressuredSurvival(committed));
        const auto& cell=cx.otherWins.timeline[clamp(time,0,TemporalOtherWins::MAX_TIME-1)][actor];
        const auto& weights=cx.projectedDeclarations(time);
        for(unsigned mask:cx.declarations.masks) {
            double survive=mask&(1u<<actor)?cell.lockedTsumoSurvival:cell.silentTsumoSurvival;
            survive=1-out.other[i].scale*(1-survive);
            out.noTsumoWeights[i][mask]=weights[mask]*survive;
        }
    }
    if(total>0)for(double& p:out.normalSources)p*=((1-callShare)/total);
    if(callShare>0)for(int p=0;p<4;++p)
        if(p!=cx.st.seat && p!=after && !cx.st.pl[p].riichi)out.extraSources[p]=callShare/canCall;
    return out;
}
inline OrderedWinInterval Evaluator::winInterval(int after,int opponentDraws,int elapsed,bool committed,bool ownDraw,int ownElapsed) {
    if(after<0 || after>=4)after=cx.st.seat;
    if(opponentDraws<0 || opponentDraws>3 || elapsed<0 || elapsed>127 || ownElapsed<0 || ownElapsed>127)
        throw runtime_error("invalid ordered-clock cache key");
    // Context, pressure parameters and timeOffset are immutable within this
    // evaluator. Returned intervals are copies: per-hand win offers never
    // mutate the cached public chronology or declaration-mask weights.
    unsigned key=unsigned(after)|(unsigned(opponentDraws)<<2)|(unsigned(committed)<<4)|
        (unsigned(elapsed)<<5)|(unsigned(ownElapsed)<<12)|(unsigned(ownDraw)<<19);
    auto found=orderedIntervals.find(key);
    if(found!=orderedIntervals.end())return found->second;
    auto out=buildWinInterval(after,opponentDraws,elapsed,committed,ownDraw,ownElapsed);
    orderedIntervals.emplace(key,out);return out;
}
inline Future Evaluator::readyOrdered(Hand& h,bool riichi) {
    Future f;U64 waits=waitMask(h);bool furi=false;
    for(U64 left=waits;left;left&=left-1)if(discardedWait(h,__builtin_ctzll(left)))furi=true;
    bool endWall=cfg.tail && cx.st.allowEndWallForecast && cx.st.remaining>0;
    bool lastSelf=endWall && tailDraws==0 && horizon>0;
    bool rootPartial=futureDraws.n==0 && horizon>0 && (cx.st.actor!=cx.st.seat || cx.st.replacementForecast);
    int after=horizon==0 && futureDraws.n==0?cx.st.actor:cx.st.seat;
    int tailElapsed=horizon==0 && futureDraws.n==0?0:max(0,cx.st.remaining-tailDraws);
    array<OrderedWinInterval,HORIZON> plans;
    plans[0]=winInterval(after,tailDraws,tailElapsed,riichi,false,tailElapsed+tailDraws);
    for(int k=1;k<HORIZON;++k) {
        bool prefix=rootPartial && k==horizon;
        int prior=prefix?max(0,distance-1):3;
        plans[k]=winInterval(prefix?cx.st.actor:cx.st.seat,prior,cycleElapsed(k),
            riichi && !deferredRiichi(riichi,k),true,ownWinTime(k));
    }
    for(U64 left=waits;left;left&=left-1) {
        if(budgetExpired())return Future{};
        int t=__builtin_ctzll(left),u=available(h,t);if(!u)continue;f.uke+=u;
        h.add(t);
        WinScore ordinary=cachedWinScore(h,t,riichi);
        WinScore last=endWall?cachedWinScore(h,t,riichi,true):ordinary;
        WinScore early=ordinary,firstTsumo=lastSelf && horizon==1?last:ordinary;
        bool deferred=deferredRiichi(riichi,horizon);
        if(rootPartial && deferred) {
            early=cachedWinScore(h,t,false);
            firstTsumo=lastSelf && horizon==1?cachedWinScore(h,t,false,true):early;
        }
        h.add(t,-1);double self=u/pool,tsumoSelf=self;array<double,4> release{};
        if(cx.constraintModel){tsumoSelf=conditionedDrawRate(t);release=conditionedReleaseRatios(t);}
        auto ronFactor=[&](bool declared) {
            double factor=t>=27?(declared?1.35:1.70):terminal(t)?(declared?1.0:1.25):(declared?.70:.85);
            if(!cx.st.closed())factor+=cfg.ron*(terminal(t)?.75:1.0)*(1-cx.threat);
            if(cx.st.dora[t])factor*=.8;
            return furi?0:factor;
        };
        plans[0].addRon(self*ronFactor(riichi)*tailDraws/3.,ordinary,last,endWall,cx.constraintModel?&release:nullptr);
        for(int k=1;k<HORIZON;++k) {
            bool prefix=rootPartial && k==horizon;
            const auto& tsumo=prefix?firstTsumo:(lastSelf && k==1?last:ordinary);
            const auto& ron=prefix?early:ordinary;
            plans[k].addTsumo(tsumoSelf,tsumo);
            plans[k].addRon(self*ronFactor(riichi && !(prefix && deferred))*plans[k].opponents/3.,ron,ron,false,cx.constraintModel?&release:nullptr);
        }
    }
    for(auto& plan:plans)plan.cap();
    double endReward=waits?(riichi?riichiReadyReward+riichiDeposit:readyReward):notenDrawValue;
    auto tail=plans[0].settle(endReward,0);f.v[0]=tail.first;f.win[0]=tail.second;
    for(int k=1;k<HORIZON;++k) {
        auto exposure=averageExposure(riichi?1.:.28,ownWinTime(k),riichi);
        double continuation=f.v[k-1],win=f.win[k-1],futureTax=exposure.loss,futureSurvival=exposure.survival,risk=f.dealIn[k-1];
        if(!riichi && retention::prefer(futureSurvival*continuation-futureTax,futureSurvival*win,exposure.dealIn(risk),foldFloor(k-1),0,0,retentionUnit())) {
            continuation=foldFloor(k-1);win=0;risk=0;futureTax=0;futureSurvival=1;
        }
        auto result=plans[k].settle(continuation,win,futureTax,futureSurvival,risk);
        f.v[k]=result.first;f.win[k]=result.second;f.dealIn[k]=result.dealIn;
    }
    if(riichi)for(int k=0;k<HORIZON;++k)f.v[k]-=riichiDeposit*(1-f.win[k]);
    f.riichi=riichi;return f;
}
inline WinScore Evaluator::estimatedWinScore(const Hand& h) {
    WinScore out;double han=estimatedHan(h);int lo=int(han),hi=min(13,lo+1);
    bool riichi=cx.st.closed() && canDeclareRiichi(cx.st);
    for(auto [hans,weight]:array<pair<int,double>,2>{{{lo,1-han+lo},{hi,han-lo}}})if(weight>0) {
        int ron=points(hans,35,cx.st.seat==0,false),tsumo=points(hans,35,cx.st.seat==0,true);
        out.ron+=weight*ron;out.tsumo+=weight*tsumo;
        for(int payer=0;payer<5;++payer)if(payer!=cx.st.seat) {
            const auto& utility=cx.declarations.ownWin(payer==4?tsumo:ron,payer==4,riichi,payer==4?-1:payer);
            for(unsigned mask:cx.declarations.masks)out.futureUtility[payer][mask]+=weight*utility[mask];
        }
    }
    out.yaku=true;return out;
}


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

// Included after Choice; no evaluation occurs while committing a candidate set.
inline string retentionDiscardAction(const Choice& c,const Context& cx,bool forcePlay=false){
    bool allowed=false;
    for(auto part:split(deadlineValidActions,',')){auto w=words(part);if(w.empty()||w[0]!="LIZHI")continue;
        if(w.size()==1)allowed=true;for(size_t i=1;i<w.size();++i)if(tile(w[i])==c.raw)allowed=true;}
    return string(!forcePlay && allowed && c.riichi && canDeclareRiichi(cx.st)?"LIZHI ":"PLAY ")+name(c.raw);
}
inline RetentionDecision discardDecision(const vector<Choice>& choices,const Context& cx){
    vector<RetentionAlternative> out;
    for(int i=0;i<int(choices.size());++i){const auto& c=choices[i];int t=norm(c.raw);
        RetentionAlternative a;a.action=retentionDiscardAction(c,cx);a.kind="attack";a.tileName=name(c.raw);a.origin=i;a.raw=c.raw;
        a.sh=c.sh;a.uke=c.uke;a.depth=c.depth;a.base=c.score;a.off=c.offense;a.conditionalWin=c.win;a.known=c.winAvailable;a.conditionalRisk=c.dealIn;a.riskBasis=standing.enabled?"modeled_current_hand":"current_discard_only_legacy";
        a.shapeAdjustment=c.shapeAdjustment;a.redAdjustment=c.redAdjustment;a.shantenAdjustment=c.shantenAdjustment;a.reserveAdjustment=c.reserveAdjustment;
        a.survival=cx.immediateSurvival[t];a.loss=cx.danger[t];a.riichi=c.riichi;a.hasImmediateDiscard=true;a.complete=c.estimateComplete;
        bool alreadyFold=c.defenseAvailable && abs(c.offense-c.defenseOff)<1e-9 && c.win==0 && !c.riichi;
        if(!alreadyFold)out.push_back(a);
        if(c.defenseAvailable){auto f=a;f.action=retentionDiscardAction(c,cx,true);f.kind="defense";f.base=c.defenseScore;f.off=c.defenseOff;
            f.conditionalWin=0;f.conditionalRisk=c.defenseRisk;f.shapeAdjustment=f.redAdjustment=f.shantenAdjustment=0;f.known=true;f.riichi=false;out.push_back(f);}
        if(c.nagashiOff>-1e20){auto n=a;n.action=retentionDiscardAction(c,cx,true);n.kind="nagashi";n.base=cx.actionValue(t,c.nagashiOff);n.off=c.nagashiOff;
            n.riskKnown=false;n.conditionalRisk=std::numeric_limits<double>::quiet_NaN();n.riskBasis="nagashi_future_exposure_not_computed";
            n.shapeAdjustment=n.redAdjustment=n.shantenAdjustment=n.reserveAdjustment=0;n.known=c.nagashiZeroWinCertified;n.conditionalWin=n.known?0:std::numeric_limits<double>::quiet_NaN();n.riichi=false;
            n.maximumOwnDraws=c.nagashiOwnDraws;n.probabilityBasis=n.known?"nagashi_no_future_calls_shanten_exceeds_own_draws":"nagashi_normal_win_not_computed";out.push_back(n);}
    }
    return RetentionDecision(move(out),"discard");
}
inline void rememberDiscard(const vector<Choice>& choices,const Context& cx){rememberRetention(discardDecision(choices,cx));}
inline Choice retainedDiscardChoice(){
    if(!retainedDecision.valid())throw runtime_error("retention: no complete valid discard candidate set");
    const auto& a=retainedDecision.chosen();Choice c;c.raw=a.raw;c.sh=a.sh;c.uke=a.uke;c.depth=a.depth;c.score=a.base;
    c.offense=a.off;c.win=a.conditionalWin;c.dealIn=a.conditionalRisk;c.winAvailable=a.known;c.riichi=a.riichi;c.estimateComplete=true;return c;
}
inline void traceDiscardProbabilities(const RetentionDecision& decision,int horizon,int tailDraws){
    if(!debugPolicy || !decision.valid())return;json rows=json::array();
    vector<int> origins;for(const auto& a:decision.alternatives)if(find(origins.begin(),origins.end(),a.origin)==origins.end())origins.push_back(a.origin);
    for(int origin:origins){int best=decision.representative(origin);if(best<0)continue;
        auto row=decision.alternatives[best].explain(decision.unit);row["selected"]=best==decision.selected;
        row["win_status"]=decision.entries[best].validWin()?"estimated_policy_current_hand_conditional_on_current_discard_passing":"policy_win_not_computed";
        rows.push_back(move(row));}
    cerr<<json({{"decision_probabilities","discard"},{"own_draw_horizon",horizon},{"tail_draws",tailDraws},{"candidates",rows}}).dump()<<'\n';
}


// Included after Choice and rememberDiscard, before chooseDiscard.
class DiscardParallel {
    struct Local {
        State state;Context cx;Evaluator ev;
        Local(const Context& parent):state(parent.st),cx(state,parent),ev(cx){}
    };
    Context& root;Evaluator& coordinator;Hand hand;ParallelSettings settings;
    unique_ptr<Context> immutableSnapshot;
    vector<unique_ptr<Local>> locals;
public:
    DiscardParallel(Context& cx,Evaluator& ev,const Hand& h):root(cx),coordinator(ev),hand(h),locals(prt::runtime.count()){
        auto begin=chrono::steady_clock::now();immutableSnapshot=make_unique<Context>(cx.st,cx);
        dt::mainSnapshotSeconds+=prt::seconds(begin,chrono::steady_clock::now());
    }
    pair<Context*,Evaluator*> get(size_t worker){
        if(!worker)return {&root,&coordinator};settings.enter(worker);
        if(!locals[worker]){ParallelSetupTimer timer{worker};locals[worker]=make_unique<Local>(*immutableSnapshot);}
        return {&locals[worker]->cx,&locals[worker]->ev};
    }
    bool refine(vector<Choice>& choices,const vector<int>& indices,int progress,int shape,bool startGate=false,bool keepUke=false){
        bool ok=parallelBatch(choices,indices,"discard",progress,shape,[&](size_t worker,int i){
            settings.enter(worker);
            if(startGate && dt::startGate(chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()>START_IMPROVE_SECONDS))throw ParallelStageUnavailable{};
            auto pointers=get(worker);auto& cx=*pointers.first;auto& ev=*pointers.second;
            Choice out=choices[i];Hand h=hand;int t=norm(out.raw);h.add(t,-1);ev.rootOut=out.raw;ev.abortable=true;
            try {
                bool done=true;Future f=shape?ev.improve(h,out.sh,&done,shape):ev.eval(h,out.sh,min(progress,out.sh));
                if(!done || ev.exhausted || sharedSearchStopped())throw ActionBudgetExpired{};
                double potential=cx.st.closed() || out.sh==0?1:openPotential(h,cx);
                double offense=ev.withPotential(f.v[ev.horizon],potential);
                double revised=out.score+cx.survivingContinuation(t,offense-out.offense);
                if(!shape || retention::prefer(out.score,cx.immediateSurvival[t]*out.win,1-cx.immediateSurvival[t]+cx.immediateSurvival[t]*out.dealIn,revised,cx.immediateSurvival[t]*f.win[ev.horizon],1-cx.immediateSurvival[t]+cx.immediateSurvival[t]*f.dealIn[ev.horizon],retentionUnit())){
                    out.score+=cx.survivingContinuation(t,offense-out.offense);out.offense=offense;
                    out.win=f.win[ev.horizon];out.dealIn=f.dealIn[ev.horizon];out.riichi=f.riichi;if(!keepUke)out.uke=f.uke;
                    if(!shape)out.depth=min(progress,out.sh);
                }
                if(sharedSearchStopped())throw ActionBudgetExpired{};
                resetHypotheticalState(ev);return out;
            }catch(...){resetHypotheticalState(ev);ev.exhausted=true;throw;}
        });
        if(ok)rememberDiscard(choices,root);else if(sharedSearchStopped())coordinator.exhausted=true;
        if(debugPolicy){json rows=json::array();for(const auto& c:choices)rows.push_back({{"tile",name(c.raw)},{"sh",c.sh},{"uke",c.uke},{"riichi",c.riichi},{"depth",c.depth},{"score",c.score},{"off",c.offense},{"win",c.win}});
            cerr<<json({{"parallel_layer","discard"},{"progress_depth",progress},{"shape_depth",shape},{"complete",ok},{"committed",rows}}).dump()<<'\n';}
        return ok;
    }
};

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

// Included inside chooseDiscard, after the complete parent-policy fallback.
// Progress estimates can fall with depth: refine ALL legal candidates
// together, never let an unsearched optimistic estimate win by default.
// Shape-only options are nonnegative refinements on a preselected beam.
#ifdef V63_SEARCH_DIAGNOSTIC
if(adaptiveEnabled)
#endif
if(deep && !ev.exhausted && cx.st.remaining>=6) {
    auto ranked=[&](int cap,bool sameSh) {
        vector<int> order;double top=-1e30;
        for(const auto& c:cs)top=max(top,searchUtility(c));
        for(int i=0;i<(int)cs.size();++i)
            if((!sameSh || cs[i].sh==minsh) && searchUtility(cs[i])+.65*retentionUnit()>=top)order.push_back(i);
        stable_sort(order.begin(),order.end(),[&](int a,int b){return searchUtility(cs[a])>searchUtility(cs[b]);});
        if((int)order.size()>cap)order.resize(cap);
        return order;
    };
    auto refine=[&](const vector<int>& selected,int progress,int shape) {
        if(selected.empty() || ev.budgetExpired())return false;
        bool complete=search.refine(cs,selected,progress,shape);
        return complete;
    };
    // Far hands used to stop at one/two progress layers even with spare time.
    vector<int> beam(cs.size());iota(beam.begin(),beam.end(),0);
    for(int depth=targetDepth+1;depth<=min(4,maxsh) && !ev.exhausted;++depth) {
#ifdef PARALLEL_REGRESSION
        if(depth>parallelTestDepth)break;
#endif
        bool useful=false;for(int i:beam)useful|=cs[i].depth<min(depth,cs[i].sh);
        if(!useful)break;
        if(!refine(beam,depth,0))break;
    }
    if(minsh<=2 && !ev.exhausted
#ifdef PARALLEL_REGRESSION
       && !parallelTestNoShape
#endif
    ) {
        // Finish first-order shape search for every competitive min-shanten
        // discard, including alternatives excluded by the old top-three cap.
        auto wide=ranked(34,true);
        if(refine(wide,0,1) && cx.st.remaining>=10) {
            // Two successive same-shanten changes, with separate depleted
            // tiles and furiten histories. No repeated pass over equal work.
            auto beam2=ranked(3,true);
            refine(beam2,0,2);
        }
    }
}

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

// Included inside namespace mj, after Context and CallOption are defined.
// These helpers only serialize values already computed by the policy. They
// never construct an evaluator, update a model, or select an action.
inline void appendDiscardDiagnostic(json& row,const Context* cx,int raw) {
    row["off_scope"]="continuation_utility";
    row["p_ron_scope"]="immediate_discard";
    if(raw<0) {
        row["p_ron"]=nullptr;row["p_ron_status"]="not_applicable";
        row["p_ron_reason"]="no_immediate_own_discard";
    } else if(cx) {
        row["p_ron"]=1-cx->immediateSurvival[norm(raw)];
        row["p_ron_status"]="estimated";
    } else {
        row["p_ron"]=nullptr;row["p_ron_status"]="not_estimated";
        row["p_ron_reason"]="context_not_evaluated";
    }
}

inline void traceUnestimatedCall(const string& action,const string& reason,
                                 const Context* cx=nullptr,int raw=-1,int group=-1) {
    if(!debugPolicy)return;
    json row={{"call_unestimated",true},{"action",action},{"reason",reason},
              {"off",nullptr},{"off_status","not_evaluated"}};
    if(raw>=0)row["tile"]=name(raw);
    if(group>=0)row["group"]=group;
    appendDiscardDiagnostic(row,cx,raw);
    cerr<<row.dump()<<'\n';
}

inline void appendKanDiagnostic(json& row,const Context* cx,const string& kind,int raw) {
    row["off_scope"]="continuation_utility";
    if(kind=="BUGANG") {
        row["p_ron_scope"]="added_kan_robbery";
        row["p_ron_model"]="ordinary_tile_model_proxy";
        if(cx && raw>=0) {
            // This is exactly the probability already used by actionValue
            // for the added kan. It is NOT a separate chankan inference.
            row["p_ron"]=1-cx->immediateSurvival[norm(raw)];
            row["p_ron_status"]="proxy";
        } else {
            row["p_ron"]=nullptr;row["p_ron_status"]="not_estimated";
            row["p_ron_reason"]="context_not_evaluated";
        }
    } else {
        row["p_ron_scope"]="concealed_kan_robbery";
        row["p_ron"]=nullptr;row["p_ron_status"]="not_modelled";
        row["p_ron_reason"]="concealed_kan_robbery_not_modelled";
    }
}

inline void traceUnestimatedKan(const string& kind,int raw,const string& reason,
                                const Context* cx=nullptr) {
    if(!debugPolicy)return;
    json row={{"kan_unestimated",true},{"kan",kind},{"tile",raw<0?json(nullptr):json(name(raw))},
              {"reason",reason},{"off",nullptr},{"off_status","not_evaluated"}};
    appendKanDiagnostic(row,cx,kind,raw);
    cerr<<row.dump()<<'\n';
}

inline void traceUnestimatedKanOffers(const string& valid,const string& reason) {
    if(!debugPolicy)return;
    for(const auto& part:split(valid,',')) {
        auto tokens=words(part);
        if(!tokens.empty() && (tokens[0]=="ANGANG" || tokens[0]=="BUGANG"))
            traceUnestimatedKan(tokens[0],-1,reason);
    }
}

inline void traceComputedKan(const string& kind,int raw,const Context& cx,double off,
                              double value,double normal,const string& status="evaluated",
                              const string& reason="") {
    if(!debugPolicy)return;
    json row={{"kan",kind},{"tile",name(raw)},{"value",value},{"normal",normal},
              {"off",off},{"off_status","evaluated"},{"candidate_status",status}};
    if(!reason.empty())row["reason"]=reason;
    appendKanDiagnostic(row,&cx,kind,raw);
    cerr<<row.dump()<<'\n';
}


// Included after CallOption, inside namespace mj.
#ifdef V59_CALL_DIAGNOSTIC
int callDepthCeiling=9,callAbortLayer=-1,callAbortAfter=-1;
bool callReverse=false,callDisableImprovement=false;
#endif

struct CallSearchGroup {
    State state;
    Context cx;
    unique_ptr<Evaluator> evaluator;
    string action;
    int a=-1,b=-1,minSh=9;
    CallSearchGroup(const State& s,const string& text="PASS",int first=-1,int second=-1):
        state(s),cx(state),action(text),a(first),b(second) {
        cx.assessThreats();evaluator=make_unique<Evaluator>(cx);
    }
    CallSearchGroup(const State& s,const Context& reference,const string& text,int first,int second,int minimum):
        state(s),cx(state,reference),action(text),a(first),b(second),minSh(minimum){evaluator=make_unique<Evaluator>(cx);}
};
struct CallSearchCandidate {
    int group=0,raw=-1,sh=9,depth=0;
    Hand hand;
    double potential=1,heuristic=0,reserve=0,closedCost=0,nagashi=-1e30;
    double score=-1e30,offense=0,win=0,dealIn=0,defenseRisk=0;
    int uke=0;
    bool estimateComplete=false,defenseAvailable=false,attackAlreadyFold=false,nagashiZeroWinCertified=false;
    int nagashiOwnDraws=0;
    double defenseOff=-1e30,defenseScore=-1e30;
    winout::CallObservation winObservation;
};


// Included after CallSearchCandidate.
class CallParallel {
    struct Snapshot {
        State state;Context cx;string action;int a,b,minSh;
        Snapshot(const CallSearchGroup& g):state(g.state),cx(state,g.cx),action(g.action),a(g.a),b(g.b),minSh(g.minSh){}
    };
    vector<unique_ptr<CallSearchGroup>>& groups;ParallelSettings settings;
    vector<unique_ptr<Snapshot>> snapshots;
    vector<unordered_map<int,unique_ptr<CallSearchGroup>>> locals;
public:
    CallParallel(vector<unique_ptr<CallSearchGroup>>& source):groups(source),locals(prt::runtime.count()){
        auto begin=chrono::steady_clock::now();for(const auto& g:groups)snapshots.push_back(make_unique<Snapshot>(*g));
        dt::mainSnapshotSeconds+=prt::seconds(begin,chrono::steady_clock::now());
    }
    CallSearchGroup& get(size_t worker,int group){if(!worker)return *groups[group];settings.enter(worker);
        auto& local=locals[worker];auto it=local.find(group);if(it==local.end()){ParallelSetupTimer timer{worker};
            const auto& s=*snapshots[group];
            it=local.emplace(group,make_unique<CallSearchGroup>(s.state,s.cx,s.action,s.a,s.b,s.minSh)).first;}
        return *it->second;
    }
    template<class Update>
    bool refine(vector<CallSearchCandidate>& choices,const vector<int>& selected,int progress,int shape,Update update,bool startGate=false){
        return parallelBatch(choices,selected,"call",progress,shape,[&](size_t worker,int index){
            settings.enter(worker);
            if(startGate && dt::startGate(chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()>START_IMPROVE_SECONDS))throw ParallelStageUnavailable{};
            auto c=choices[index];auto& g=get(worker,c.group);auto& ev=*g.evaluator;ev.rootOut=c.raw;ev.abortable=true;Hand h=c.hand;
            try {
                bool done=true;auto future=shape?ev.improve(h,c.sh,&done,shape):ev.eval(h,c.sh,min(progress,c.sh));
                if(!done || ev.exhausted || sharedSearchStopped())throw ActionBudgetExpired{};
                auto next=c;update(next,future,g);double survival=c.raw<0?1:g.cx.immediateSurvival[norm(c.raw)];
                if(!shape || retention::prefer(c.score,survival*c.win,1-survival+survival*c.dealIn,next.score,survival*next.win,1-survival+survival*next.dealIn,retentionUnit()))c=next;
                if(!shape)c.depth=min(progress,c.sh);
                if(sharedSearchStopped())throw ActionBudgetExpired{};
                resetHypotheticalState(ev);return c;
            }catch(...){resetHypotheticalState(ev);ev.exhausted=true;throw;}
        });
    }
};


string compareCallsTogether(State& st,const vector<CallOption>& options) {
    vector<unique_ptr<CallSearchGroup>> groups;
    groups.push_back(make_unique<CallSearchGroup>(st));
    auto& base=*groups[0];int oldsh=shanten(st.hand,base.cx.melds);
    vector<CallSearchCandidate> choices;
    CallSearchCandidate pass;pass.hand=st.hand;pass.sh=oldsh;
    pass.nagashi=base.evaluator->nagashiValue(pass.hand);choices.push_back(pass);
    for(const auto& option:options) {
        State next=st;next.add(option.a,-1);next.add(option.b,-1);
        ++next.publicSeen[norm(option.a)];++next.publicSeen[norm(option.b)];
        Meld meld;meld.tiles={st.lastDiscard,option.a,option.b};meld.kind=option.action=="PENG"?1:0;
        meld.called=st.lastDiscard;meld.from=st.lastPlayer;next.pl[st.seat].melds.push_back(meld);next.actor=st.seat;
        int group=int(groups.size());groups.push_back(make_unique<CallSearchGroup>(next,option.action,option.a,option.b));
        auto& g=*groups.back();vector<CallSearchCandidate> candidates;
        for(int t=0;t<34;++t)if(next.hand.c[t] && !option.ban[t]) {
            int raw=next.actual(t);if(raw<0)continue;
            CallSearchCandidate c;c.group=group;c.raw=raw;c.hand=next.hand;c.hand.add(t,-1);
            c.sh=shanten(c.hand,g.cx.melds);g.minSh=min(g.minSh,c.sh);c.potential=openPotential(c.hand,g.cx);
            candidates.push_back(c);
        }
        for(auto c:candidates) {
            // Preserve the existing call policy's no-shanten-loss and yaku
            // eligibility, while retaining every eligible discard candidate.
            if(c.sh>oldsh || st.closed() && c.potential<.75 && st.remaining>12) {
                if(debugPolicy)traceUnestimatedCall(g.action,c.sh>oldsh?"shanten_loss_filter":"open_yaku_filter",&g.cx,c.raw,group);
                continue;
            }
            c.heuristic=shapeValue(c.hand,g.cx)*3.-(c.raw>=34?25:0);
            c.heuristic-=max(0,c.sh-g.minSh)*(g.cx.threat>.6?160.:420.)*min(1.,st.remaining/24.);
            if(g.cx.threat>.45 && c.sh>0) {
                double safest=1e30;
                for(int t=0;t<34;++t)if(c.hand.c[t])safest=min(safest,g.cx.danger[t]);
                if(safest<1e20)c.reserve=min(250.,safest)*.18;
            }
            c.heuristic-=c.reserve;c.closedCost=st.closed()?130+70*c.sh:0;
            g.evaluator->rootOut=c.raw;c.nagashi=g.evaluator->nagashiValue(c.hand,c.raw);
            choices.push_back(c);
        }
    }
    if(choices.size()==1) {
        dt::reason("all_call_candidates_filtered");
        if(debugPolicy)traceUnestimatedCall("PASS","all_call_discards_filtered");
        return "PASS";
    }
    auto updateIn=[&](CallSearchCandidate& c,const Future& future,CallSearchGroup& g) {
        auto& ev=*g.evaluator;double offense=future.v[ev.horizon];
        if(c.group && c.sh>0)offense=ev.withPotential(offense,c.potential);
        c.offense=offense;c.win=future.win[ev.horizon];c.dealIn=future.dealIn[ev.horizon];c.uke=future.uke;
        c.score=c.group?g.cx.actionValue(norm(c.raw),offense)+c.heuristic-c.closedCost:offense;
        c.defenseAvailable=standing.enabled;c.defenseOff=ev.foldFloor(ev.horizon);
        c.defenseScore=c.group?g.cx.actionValue(norm(c.raw),c.defenseOff)-c.reserve-c.closedCost:c.defenseOff;
        c.attackAlreadyFold=c.defenseAvailable && abs(offense-c.defenseOff)<1e-9 && c.win==0 && !future.riichi;
        c.nagashiOwnDraws=ev.horizon;c.nagashiZeroWinCertified=retention::nagashiZeroWinCertificate(c.sh,ev.horizon);c.estimateComplete=true;
    };
    auto update=[&](CallSearchCandidate& c,const Future& f){updateIn(c,f,*groups[c.group]);};
    auto evaluate=[&](CallSearchCandidate& c,int depth) {
        auto& ev=*groups[c.group]->evaluator;ev.rootOut=c.raw;
        Hand h=c.hand;auto future=ev.eval(h,c.sh,min(depth,c.sh));update(c,future);c.depth=min(depth,c.sh);
    };
    auto searchUtility=[&](const CallSearchCandidate& c){double survival=c.raw<0?1:groups[c.group]->cx.immediateSurvival[norm(c.raw)];
        return retention::objective({c.score-(c.group?90:0),survival*c.win,true,true,1-survival+survival*c.dealIn},retentionUnit());};
    auto totalNodes=[&](){uint64_t total=0;for(auto& g:groups)total+=g->evaluator->nodes;return total;};
    auto timedOut=[&](){return dt::timeLimit(chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()>SEARCH_SECONDS);};
    auto decisionFor=[&](const vector<CallSearchCandidate>& cs){
        vector<RetentionAlternative> all;
        for(int i=0;i<int(cs.size());++i){const auto& c=cs[i];const auto& g=*groups[c.group];RetentionAlternative a;
            a.origin=i;a.raw=c.raw;a.tileName=c.raw<0?"":name(c.raw);a.sh=c.sh;a.uke=c.uke;a.depth=c.depth;
            a.action=c.group?g.action+" "+name(st.lastDiscard)+" "+name(g.a)+" "+name(g.b)+" "+name(c.raw):"PASS";
            a.base=c.score;a.off=c.offense;a.conditionalWin=c.win;a.conditionalRisk=c.dealIn;a.riskBasis=standing.enabled?"modeled_current_hand":"current_discard_only_legacy";
            a.shapeAdjustment=c.heuristic+c.reserve;a.reserveAdjustment=-c.reserve;a.closedAdjustment=-c.closedCost;a.switchCost=c.group?90:0;
            a.survival=c.raw<0?1:g.cx.immediateSurvival[norm(c.raw)];a.loss=c.raw<0?0:g.cx.danger[norm(c.raw)];a.hasImmediateDiscard=c.raw>=0;
            a.complete=c.estimateComplete;
            if(!c.attackAlreadyFold)all.push_back(a);
            if(c.defenseAvailable){auto f=a;f.kind="defense";f.base=c.defenseScore;f.off=c.defenseOff;f.conditionalWin=0;f.conditionalRisk=c.defenseRisk;f.shapeAdjustment=0;all.push_back(f);}
            if(c.nagashi>-1e20){auto n=a;n.kind="nagashi";n.off=c.nagashi;n.base=c.group?g.cx.actionValue(norm(c.raw),c.nagashi)-c.closedCost:c.nagashi;
                n.riskKnown=false;n.conditionalRisk=std::numeric_limits<double>::quiet_NaN();n.riskBasis="nagashi_future_exposure_not_computed";n.shapeAdjustment=n.reserveAdjustment=0;n.known=c.nagashiZeroWinCertified;n.conditionalWin=n.known?0:std::numeric_limits<double>::quiet_NaN();
                n.maximumOwnDraws=c.nagashiOwnDraws;n.probabilityBasis=n.known?"nagashi_no_future_calls_shanten_exceeds_own_draws":"nagashi_normal_win_not_computed";all.push_back(n);}
        }
        return RetentionDecision(move(all),"call");
    };
    auto snapshot=[&](const vector<CallSearchCandidate>& cs){
        auto decision=decisionFor(cs);json rows=json::array();
        for(int i=0;i<int(cs.size());++i){const auto& c=cs[i];int best=decision.representative(i);if(best<0)continue;
            const auto& a=decision.alternatives[best];const auto& e=decision.entries[best];
            json row=a.explain(decision.unit);row["index"]=i;row["group"]=c.group;row["action"]=groups[c.group]->action;
            row["off_status"]="evaluated";row["selected"]=decision.valid()&&i==decision.chosen().origin;
            appendDiscardDiagnostic(row,&groups[c.group]->cx,c.raw);rows.push_back(move(row));
        }
        return rows;
    };
    auto rememberCallChoice=[&](){return rememberRetention(decisionFor(choices));};
    int minsh=9,maxsh=0;
    for(auto& c:choices){evaluate(c,0);
#ifdef RETENTION_FAULT_TEST
        retentionFaultPoint(2);
#endif
        if(groups[c.group]->evaluator->exhausted || sharedSearchStopped())throw ActionBudgetExpired{};
        minsh=min(minsh,c.sh);maxsh=max(maxsh,c.sh);}
    rememberCallChoice();
    if(retentionBaselineMode)return deadlineResponse;
    if(debugPolicy)cerr<<json({{"fair_call_layer",0},{"complete",true},{"nodes",totalNodes()},{"committed",snapshot(choices)}}).dump()<<'\n';
    int target=minsh<=2?min(3,maxsh):oldsh==3 && st.remaining>=16?2:1;
#ifdef V59_CALL_DIAGNOSTIC
    target=min(target,callDepthCeiling);
#endif
#ifdef PARALLEL_REGRESSION
    target=min(target,parallelTestDepth);
#endif
    for(auto& g:groups)g->evaluator->abortable=true;
    CallParallel search(groups);bool mainComplete=true;
    auto parallelRefine=[&](const vector<int>& selected,int progress,int shape,bool gate=false){
        bool ok=search.refine(choices,selected,progress,shape,updateIn,gate);if(ok)rememberCallChoice();
        if(debugPolicy)cerr<<json({{"parallel_layer","call"},{"progress_depth",progress},{"shape_depth",shape},{"complete",ok},{"committed",snapshot(choices)}}).dump()<<'\n';
        return ok;
    };
    for(int depth=1;depth<=target;++depth){
        vector<int> selected;for(int i=0;i<int(choices.size());++i)if(choices[i].depth<min(depth,choices[i].sh))selected.push_back(i);
        if(!parallelRefine(selected,depth,0)){mainComplete=false;break;}
    }
    // Cangfeng's useful principle: account for keeping a valuable closed
    // hand. Every selected call improvement competes with the PASS option's
    // improvement in one atomic pass, never with a stale PASS baseline.
    if(
#ifdef V59_CALL_DIAGNOSTIC
       !callDisableImprovement &&
#endif
#ifdef PARALLEL_REGRESSION
       !parallelTestNoShape &&
#endif
       mainComplete && oldsh<=IMPROVE_SHANTEN && st.remaining>=6 &&
       dt::startAllowed(chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()<START_IMPROVE_SECONDS)) {
        vector<int> selected;
        double best=searchUtility(choices[0]);for(const auto& c:choices)best=max(best,searchUtility(c));
        for(int i=1;i<(int)choices.size();++i)if(choices[i].sh<=IMPROVE_SHANTEN && searchUtility(choices[i])+.65*retentionUnit()>=best)selected.push_back(i);
        stable_sort(selected.begin(),selected.end(),[&](int a,int b){return searchUtility(choices[a])>searchUtility(choices[b]);});
        if(selected.size()>3)selected.resize(3);selected.insert(selected.begin(),0);
        bool complete=parallelRefine(selected,0,1,true);
        if(debugPolicy)cerr<<json({{"fair_call_improvement",true},{"complete",complete},{"selected",selected},
            {"nodes",totalNodes()},{"committed",snapshot(choices)}}).dump()<<'\n';
    }

// PASS participates in each extra layer, so a deeper call is never compared
// only against its stale PASS valuation. The original result remains saved.
#ifdef V63_SEARCH_DIAGNOSTIC
if(adaptiveEnabled)
#endif
if(mainComplete && st.remaining>=10 && !timedOut()) {
    double top=searchUtility(choices[0]);for(const auto& c:choices)top=max(top,searchUtility(c));
    vector<int> selected;
    for(int i=1;i<(int)choices.size();++i)
        if(searchUtility(choices[i])+.65*retentionUnit()>=top)selected.push_back(i);
    stable_sort(selected.begin(),selected.end(),[&](int a,int b){return searchUtility(choices[a])>searchUtility(choices[b]);});
    if(selected.size()>3)selected.resize(3);selected.insert(selected.begin(),0);
    auto refine=[&](const vector<int>& selected,int progress,int shape) {
        bool complete=parallelRefine(selected,progress,shape);
        return complete;
    };
    vector<int> all(choices.size());iota(all.begin(),all.end(),0);
    bool ok=true;
    for(int depth=target+1;depth<=min(4,maxsh) && ok;++depth) {
#ifdef PARALLEL_REGRESSION
        if(depth>parallelTestDepth)break;
#endif
        bool useful=false;for(int i:all)useful|=choices[i].depth<min(depth,choices[i].sh);
        if(!useful)break;ok=refine(all,depth,0);
    }
    if(ok && oldsh<=2 && !timedOut()
#ifdef PARALLEL_REGRESSION
       && !parallelTestNoShape
#endif
    )refine(selected,0,2);
}

    rememberCallChoice();
    if(!retainedDecision.valid())throw runtime_error("retention: no complete valid call candidate set");
    int best=retainedDecision.chosen().origin;
    if(debugPolicy)cerr<<json({{"fair_call_choice",best},{"selected_value",retention::objective(retainedDecision.entries[retainedDecision.selected],retainedDecision.unit)},
        {"main_complete",mainComplete},{"all_candidates",snapshot(choices)}}).dump()<<'\n';
    if(!retentionBaselineMode)cerr<<json({{"search_summary","call"},{"nodes",totalNodes()},{"candidates",choices.size()},
        {"seconds",chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()},{"chosen",best}}).dump()<<'\n';
    return deadlineResponse;

}

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
