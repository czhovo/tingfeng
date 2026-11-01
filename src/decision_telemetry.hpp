#pragma once
#include <process.h>
#include "telemetry_build.hpp"

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
