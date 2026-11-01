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
