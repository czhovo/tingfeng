#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cpuid.h>

namespace prt {
using Clock=std::chrono::steady_clock;
inline double seconds(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double>(b-a).count();}
inline uint64_t filetime(FILETIME x){return (uint64_t(x.dwHighDateTime)<<32)|x.dwLowDateTime;}
inline double threadCpu(){FILETIME a,b,k,u;if(!GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u))return -1;return (filetime(k)+filetime(u))*1e-7;}
inline std::string hexMask(uint64_t x){std::ostringstream s;s<<"0x"<<std::hex<<x;return s.str();}
struct RuntimeError:std::runtime_error {
    DWORD code;std::string operation;
    RuntimeError(const std::string& op,DWORD error):std::runtime_error(op+" failed, Win32="+std::to_string(error)),code(error),operation(op){}
};
inline void requireWin(bool ok,const char* operation){if(!ok)throw RuntimeError(operation,GetLastError());}
struct Cpu {unsigned core=0,group=0,logical=0,efficiency=0;ULONG setId=0;bool allocatedElsewhere=false;};
struct Core {unsigned index=0,group=0,efficiency=0;uint64_t mask=0;bool smt=false;};
using GetSetsFn=BOOL(WINAPI*)(void*,ULONG,PULONG,HANDLE,ULONG);
using SetProcessSetsFn=BOOL(WINAPI*)(HANDLE,const ULONG*,ULONG);
using GetProcessSetsFn=BOOL(WINAPI*)(HANDLE,PULONG,ULONG,PULONG);
using SetThreadSetsFn=BOOL(WINAPI*)(HANDLE,const ULONG*,ULONG);
using GetThreadSetsFn=BOOL(WINAPI*)(HANDLE,PULONG,ULONG,PULONG);
struct Api {
    GetSetsFn getSets=nullptr;SetProcessSetsFn setProcessSets=nullptr;GetProcessSetsFn getProcessSets=nullptr;
    SetThreadSetsFn setThreadSets=nullptr;GetThreadSetsFn getThreadSets=nullptr;
    void load(){auto k=GetModuleHandleA("kernel32.dll");
        getSets=reinterpret_cast<GetSetsFn>(GetProcAddress(k,"GetSystemCpuSetInformation"));
        setProcessSets=reinterpret_cast<SetProcessSetsFn>(GetProcAddress(k,"SetProcessDefaultCpuSets"));
        getProcessSets=reinterpret_cast<GetProcessSetsFn>(GetProcAddress(k,"GetProcessDefaultCpuSets"));
        setThreadSets=reinterpret_cast<SetThreadSetsFn>(GetProcAddress(k,"SetThreadSelectedCpuSets"));
        getThreadSets=reinterpret_cast<GetThreadSetsFn>(GetProcAddress(k,"GetThreadSelectedCpuSets"));
        if(!getSets||!setProcessSets||!getProcessSets||!setThreadSets||!getThreadSets)throw RuntimeError("required CPU-set API unavailable",ERROR_PROC_NOT_FOUND);
    }
};
struct RequestControl {
    bool active=false;uint64_t sequence=0;Clock::time_point start{},deadline{};
    std::atomic<bool> cancelled{false},timeStop{false};std::atomic<uint64_t> chargedNodes{0};
    std::mutex unionMutex;int unionActive=0,unionPeak=0;Clock::time_point unionStart{};double unionSeconds=0;
    double dispatchSeconds=0,joinWaitSeconds=0;unsigned batches=0,tasksScheduled=0,tasksCompleted=0,incompleteBatches=0;
    std::atomic<unsigned> runningJobs{0};double mainCpuStart=0;
    void begin(uint64_t seq,Clock::time_point started,double budget){
        active=true;sequence=seq;start=started;deadline=start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(budget));
        cancelled=false;timeStop=false;chargedNodes=0;unionActive=unionPeak=0;unionSeconds=0;
        dispatchSeconds=joinWaitSeconds=0;batches=tasksScheduled=tasksCompleted=incompleteBatches=0;runningJobs=0;mainCpuStart=threadCpu();
    }
    bool stop(){if(!active)return false;if(Clock::now()>=deadline){timeStop=true;cancelled=true;}return cancelled.load(std::memory_order_relaxed);}
    // Observation only: node counts never cancel a request. The common wall deadline is unchanged.
    void charge(){if(active)chargedNodes.fetch_add(1,std::memory_order_relaxed);}
    void searchEnter(){if(!active)return;std::lock_guard<std::mutex> l(unionMutex);if(unionActive++==0)unionStart=Clock::now();unionPeak=std::max(unionPeak,unionActive);}
    void searchLeave(){if(!active)return;std::lock_guard<std::mutex> l(unionMutex);if(--unionActive==0)unionSeconds+=seconds(unionStart,Clock::now());}
};
inline RequestControl request;
struct Worker {
    std::thread thread;DWORD threadId=0;json binding;bool ready=false,ok=false;std::string error,requestError;
    uint64_t requestSeen=0;double cpuStart=0,cpuSeconds=0,setupSeconds=0;unsigned batches=0;
};
class Runtime {
    Api api;std::vector<Core> cores;std::vector<Cpu> cpuSets;std::vector<Cpu> selected;
    std::vector<std::unique_ptr<Worker>> workers;
    std::mutex mutex;std::condition_variable wake,finished;uint64_t generation=0;int pending=0;
    bool quitting=false,started=false;std::function<void(size_t)> job;
    uint64_t originalMask=0,processMask=0,systemMask=0;DWORD processClass=0;
    json topology;double startupSeconds=0;unsigned joinedThreads=0;
    json bind(size_t id){const auto& cpu=selected.at(id);
        requireWin(api.setThreadSets(GetCurrentThread(),&cpu.setId,1),"SetThreadSelectedCpuSets");
        GROUP_AFFINITY desired{},previous{};desired.Group=WORD(cpu.group);desired.Mask=KAFFINITY(1)<<cpu.logical;
        requireWin(SetThreadGroupAffinity(GetCurrentThread(),&desired,&previous),"SetThreadGroupAffinity");
        requireWin(SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL),"SetThreadPriority(normal relative to HIGH class)");
        GROUP_AFFINITY actual{};requireWin(GetThreadGroupAffinity(GetCurrentThread(),&actual),"GetThreadGroupAffinity");
        ULONG count=0;api.getThreadSets(GetCurrentThread(),nullptr,0,&count);std::vector<ULONG> sets(count);
        requireWin(api.getThreadSets(GetCurrentThread(),sets.data(),count,&count),"GetThreadSelectedCpuSets");
        PROCESSOR_NUMBER current{};GetCurrentProcessorNumberEx(&current);
        bool exact=actual.Group==cpu.group && actual.Mask==(KAFFINITY(1)<<cpu.logical) && sets.size()==1 && sets[0]==cpu.setId;
        if(!exact)throw RuntimeError("thread affinity verification mismatch",ERROR_INVALID_DATA);
        return {{"worker",id},{"physical_core",cpu.core},{"efficiency_class",cpu.efficiency},{"logical_cpu",cpu.logical},{"group",cpu.group},
            {"thread_id",GetCurrentThreadId()},{"requested_mask",hexMask(uint64_t(desired.Mask))},{"actual_mask",hexMask(uint64_t(actual.Mask))},
            {"actual_group",actual.Group},{"cpu_set_ids",sets},{"observed_cpu",current.Number},{"observed_group",current.Group},
            {"thread_priority_relative",GetThreadPriority(GetCurrentThread())},{"process_priority_class",GetPriorityClass(GetCurrentProcess())},{"verified",exact}};
    }
    void run(size_t id,const std::function<void(size_t)>& f){
        auto& w=*workers[id];if(w.requestSeen!=request.sequence){w.requestSeen=request.sequence;w.cpuStart=threadCpu();w.setupSeconds=0;w.batches=0;w.requestError.clear();}
        request.runningJobs.fetch_add(1);try{f(id);}catch(const std::exception& e){w.requestError=e.what();request.cancelled=true;}catch(...){w.requestError="unclassified worker exception";request.cancelled=true;}
        request.runningJobs.fetch_sub(1);++w.batches;
        double cpu=threadCpu();w.cpuSeconds=cpu>=0 && w.cpuStart>=0?cpu-w.cpuStart:-1;
        GROUP_AFFINITY actual{};bool ok=GetThreadGroupAffinity(GetCurrentThread(),&actual);
        w.ok=ok && actual.Group==selected[id].group && actual.Mask==(KAFFINITY(1)<<selected[id].logical);
        if(!w.ok){w.error="affinity changed during request";request.cancelled=true;}
    }
public:
    bool runtimeInfoOnly=false,analyze=false;unsigned requestedWorkers=0;
    size_t count()const{return selected.size();}
    void configure(int argc,char** argv){
        for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--analyze")analyze=true;else if(a=="--runtime-info")runtimeInfoOnly=true;
            else if(a.rfind("--workers=",0)==0)requestedWorkers=unsigned(std::stoul(a.substr(10)));
            else if(a=="--workers" && i+1<argc)requestedWorkers=unsigned(std::stoul(argv[++i]));}
        api.load();DWORD_PTR p=0,s=0;requireWin(GetProcessAffinityMask(GetCurrentProcess(),&p,&s),"GetProcessAffinityMask(initial)");originalMask=p;systemMask=s;
        if(GetActiveProcessorGroupCount()!=1)throw RuntimeError("multiple processor groups not validated; refusing ambiguous binding",ERROR_NOT_SUPPORTED);
        DWORD bytes=0;GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);std::vector<BYTE> data(bytes);
        requireWin(GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()),&bytes),"GetLogicalProcessorInformationEx");
        std::set<unsigned> classes;
        for(size_t offset=0;offset<bytes;){auto info=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()+offset);
            if(!info->Size || offset+info->Size>bytes)throw RuntimeError("invalid processor topology",ERROR_INVALID_DATA);
            if(info->Relationship==RelationProcessorCore){auto& c=info->Processor;
                if(c.GroupCount!=1 || c.GroupMask[0].Group!=0)throw RuntimeError("unsupported physical-core group mapping",ERROR_NOT_SUPPORTED);
                cores.push_back({unsigned(cores.size()),c.GroupMask[0].Group,c.EfficiencyClass,uint64_t(c.GroupMask[0].Mask),bool(c.Flags&LTP_PC_SMT)});classes.insert(c.EfficiencyClass);}
            offset+=info->Size;}
        if(classes.size()<2)throw RuntimeError("efficiency classes do not distinguish P/E cores",ERROR_NOT_SUPPORTED);
        ULONG size=0;api.getSets(nullptr,0,&size,GetCurrentProcess(),0);std::vector<BYTE> sets(size);
        requireWin(api.getSets(sets.data(),size,&size,GetCurrentProcess(),0),"GetSystemCpuSetInformation");
        for(size_t offset=0;offset<size;){BYTE* b=sets.data()+offset;ULONG len=0,type=0,id=0;WORD group=0;
            std::memcpy(&len,b,4);std::memcpy(&type,b+4,4);if(len<32 || offset+len>size)throw RuntimeError("invalid CPU-set topology",ERROR_INVALID_DATA);
            if(type==0){std::memcpy(&id,b+8,4);std::memcpy(&group,b+12,2);Cpu cpu;cpu.group=group;cpu.logical=b[14];cpu.efficiency=b[18];cpu.setId=id;cpu.allocatedElsewhere=(b[19]&2) && !(b[19]&4);
                auto core=std::find_if(cores.begin(),cores.end(),[&](const Core& c){return c.group==cpu.group && (c.mask&(uint64_t(1)<<cpu.logical));});
                if(core==cores.end() || core->efficiency!=cpu.efficiency)throw RuntimeError("CPU-set/core efficiency disagreement",ERROR_INVALID_DATA);
                cpu.core=core->index;cpuSets.push_back(cpu);}
            offset+=len;}
        unsigned highest=*classes.rbegin();json coreRows=json::array();
        for(const auto& c:cores){json lps=json::array();for(unsigned n=0;n<64;++n)if(c.mask&(uint64_t(1)<<n))lps.push_back(n);
            coreRows.push_back({{"physical_core",c.index},{"group",c.group},{"efficiency_class",c.efficiency},{"classification",c.efficiency==highest?"P":"E"},{"smt",c.smt},{"logical_cpus",lps},{"mask",hexMask(c.mask)}});
            if(c.efficiency==highest){auto options=cpuSets;std::sort(options.begin(),options.end(),[](const Cpu&a,const Cpu&b){return a.logical<b.logical;});
                auto hit=std::find_if(options.begin(),options.end(),[&](const Cpu& x){return x.core==c.index && !x.allocatedElsewhere && (originalMask&(uint64_t(1)<<x.logical));});
                if(hit!=options.end())selected.push_back(*hit);}}
        unsigned available=selected.size();if(!available)throw RuntimeError("no available performance core",ERROR_NOT_SUPPORTED);
        if(requestedWorkers>available)throw RuntimeError("worker count exceeds available physical P cores",ERROR_INVALID_PARAMETER);
        if(requestedWorkers)selected.resize(requestedWorkers);
        std::vector<ULONG> ids;for(const auto& c:selected){processMask|=uint64_t(1)<<c.logical;ids.push_back(c.setId);}
        requireWin(SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS),"SetPriorityClass(HIGH_PRIORITY_CLASS)");
        requireWin(SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(processMask)),"SetProcessAffinityMask(P representatives)");
        requireWin(api.setProcessSets(GetCurrentProcess(),ids.data(),ids.size()),"SetProcessDefaultCpuSets");
        requireWin(GetProcessAffinityMask(GetCurrentProcess(),&p,&s),"GetProcessAffinityMask(verify)");processClass=GetPriorityClass(GetCurrentProcess());
        ULONG gotCount=0;api.getProcessSets(GetCurrentProcess(),nullptr,0,&gotCount);std::vector<ULONG> actualSets(gotCount);
        requireWin(api.getProcessSets(GetCurrentProcess(),actualSets.data(),gotCount,&gotCount),"GetProcessDefaultCpuSets");
        auto sorted=ids;std::sort(sorted.begin(),sorted.end());std::sort(actualSets.begin(),actualSets.end());
        if(p!=processMask || processClass!=HIGH_PRIORITY_CLASS || actualSets!=sorted)throw RuntimeError("process priority/affinity verification mismatch",ERROR_INVALID_DATA);
        char brand[49]{};unsigned a,b,c,d;if(__get_cpuid_max(0x80000000,nullptr)>=0x80000004)for(int leaf=0;leaf<3;++leaf){__cpuid(0x80000002+leaf,a,b,c,d);std::memcpy(brand+leaf*16,&a,4);std::memcpy(brand+leaf*16+4,&b,4);std::memcpy(brand+leaf*16+8,&c,4);std::memcpy(brand+leaf*16+12,&d,4);}
        topology={{"cpu_brand",brand},{"cores",coreRows},{"physical_P_cores_available",available},{"workers",selected.size()},
            {"original_process_mask",hexMask(originalMask)},{"system_mask",hexMask(systemMask)},{"process_mask",hexMask(processMask)},
            {"process_cpu_set_ids",actualSets},{"priority_class",processClass},{"priority_name","HIGH_PRIORITY_CLASS"},{"pid",GetCurrentProcessId()},
            {"classification_source","GetLogicalProcessorInformationEx + GetSystemCpuSetInformation EfficiencyClass; maximum class=P"},
            {"selection_policy","one lowest available logical CPU per physical P core; no E cores or SMT duplicate workers"}};
        workers.reserve(count());for(size_t id=0;id<count();++id)workers.push_back(std::make_unique<Worker>());
        workers[0]->binding=bind(0);workers[0]->threadId=GetCurrentThreadId();workers[0]->ready=workers[0]->ok=true;
    }
    void start(const std::function<void()>& warm){if(started)return;auto begin=Clock::now();started=true;warm();
        for(size_t id=1;id<count();++id)workers[id]->thread=std::thread([&,id,warm]{auto& w=*workers[id];
            try{w.binding=bind(id);w.threadId=GetCurrentThreadId();warm();w.ok=true;}catch(const std::exception&e){w.error=e.what();w.ok=false;}
            {std::lock_guard<std::mutex> l(mutex);w.ready=true;finished.notify_all();}if(!w.ok)return;
            uint64_t seen=0;for(;;){const std::function<void(size_t)>* work=nullptr;{std::unique_lock<std::mutex> l(mutex);wake.wait(l,[&]{return quitting||generation!=seen;});if(quitting)break;seen=generation;work=&job;}
                run(id,*work);{std::lock_guard<std::mutex> l(mutex);--pending;finished.notify_all();}}
        });
        {std::unique_lock<std::mutex> l(mutex);finished.wait(l,[&]{for(const auto&w:workers)if(!w->ready)return false;return true;});}
        for(auto&w:workers)if(!w->ok){std::string e=w->error;shutdown();throw std::runtime_error("worker startup: "+e);}
        startupSeconds=seconds(begin,Clock::now());
    }
    void batch(const std::function<void(size_t)>& f){if(!started)throw std::runtime_error("pool not started");auto start=Clock::now();
        {std::lock_guard<std::mutex> l(mutex);if(pending)throw std::runtime_error("overlapping search batches");job=f;pending=int(count())-1;++generation;}
        wake.notify_all();request.dispatchSeconds+=seconds(start,Clock::now());++request.batches;run(0,f);
        auto wait=Clock::now();{std::unique_lock<std::mutex> l(mutex);finished.wait(l,[&]{return pending==0;});job={};}
        request.joinWaitSeconds+=seconds(wait,Clock::now());
        for(auto&w:workers)if(!w->ok)throw std::runtime_error(w->error);
        if(request.runningJobs.load()!=0)throw std::runtime_error("search work still active after barrier");
    }
    void addSetup(size_t id,double t){workers.at(id)->setupSeconds+=t;}
    json evidence()const{auto out=topology;out["worker_bindings"]=json::array();for(const auto&w:workers)out["worker_bindings"].push_back(w->binding);
        out["thread_pool_startup_seconds"]=startupSeconds;out["persistent_background_threads"]=started && !quitting?count()-1:0;out["joined_threads"]=joinedThreads;return out;}
    json requestEvidence()const{json out={{"configured_workers",count()},{"batches",request.batches},{"tasks_scheduled",request.tasksScheduled},
        {"tasks_completed",request.tasksCompleted},{"incomplete_batches",request.incompleteBatches},{"active_jobs_at_emit",request.runningJobs.load()},
        {"global_budget_node_charges",request.chargedNodes.load()},{"global_node_limit",nullptr},{"max_parallel_search_threads",request.unionPeak},
        {"dispatch_bookkeeping_seconds",request.dispatchSeconds},{"main_join_wait_seconds",request.joinWaitSeconds},
        {"idle_background_threads",started&&!quitting?count()-1:0},{"workers",json::array()}};
        for(size_t id=0;id<count();++id){const auto&w=*workers[id];bool used=w.requestSeen==request.sequence;out["workers"].push_back({{"id",id},
            {"logical_cpu",selected[id].logical},{"physical_core",selected[id].core},{"thread_id",w.threadId},{"affinity_verified",w.ok},
            {"used",used},{"batches",used?w.batches:0},{"cpu_seconds",used && w.cpuSeconds<0?json(nullptr):json(used?w.cpuSeconds:0)},
            {"context_setup_seconds",used?w.setupSeconds:0},{"error",used&&!w.requestError.empty()?json(w.requestError):json(nullptr)}});}
        double cpu=threadCpu();out["main_thread_cpu_seconds"]=cpu>=0&&request.mainCpuStart>=0?json(cpu-request.mainCpuStart):json(nullptr);
        return out;}
    void shutdown(){if(!started||quitting)return;{std::lock_guard<std::mutex> l(mutex);quitting=true;}wake.notify_all();
        for(size_t id=1;id<count();++id)if(workers[id]->thread.joinable()){workers[id]->thread.join();++joinedThreads;}
    }
    ~Runtime(){shutdown();}
};
inline Runtime runtime;
}
