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

#include "parallel_call.hpp"

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
    #include "adaptive_call_search.hpp"
    rememberCallChoice();
    if(!retainedDecision.valid())throw runtime_error("retention: no complete valid call candidate set");
    int best=retainedDecision.chosen().origin;
    if(debugPolicy)cerr<<json({{"fair_call_choice",best},{"selected_value",retention::objective(retainedDecision.entries[retainedDecision.selected],retainedDecision.unit)},
        {"main_complete",mainComplete},{"all_candidates",snapshot(choices)}}).dump()<<'\n';
    if(!retentionBaselineMode)cerr<<json({{"search_summary","call"},{"nodes",totalNodes()},{"candidates",choices.size()},
        {"seconds",chrono::duration<double>(chrono::steady_clock::now()-actionStarted).count()},{"chosen",best}}).dump()<<'\n';
    return deadlineResponse;

}
