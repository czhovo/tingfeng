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
