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
