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
