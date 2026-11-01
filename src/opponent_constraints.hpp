#pragma once
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
