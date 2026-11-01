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
