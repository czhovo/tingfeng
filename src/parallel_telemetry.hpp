#pragma once
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
