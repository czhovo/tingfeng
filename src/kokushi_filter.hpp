#pragma once

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
