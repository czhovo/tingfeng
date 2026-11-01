#pragma once
namespace infer {
struct RiskPosterior {
    double ready=0,pursuit=0;bool structurallyImpossible=false;
    array<double,34> ron{},doubleRon{};
    array<double,3> seedReady{};
};
inline thread_local unordered_map<string,RiskPosterior> actionCache;
inline RiskPosterior predict(const array<int,34>& unseen,const Player& player,ohc::Model* constraintModel=nullptr,int constraintSeat=-1) {
    int missing=0;bool pairAvailable=false;
    for(int t=0;t<34;++t)if(terminal(t)){missing+=unseen[t]==0;pairAvailable|=unseen[t]>=2;}
    if(missing>=2 || (missing==1 && !pairAvailable)) {
        RiskPosterior impossible;impossible.structurallyImpossible=true;return impossible;
    }
    array<int,34> pool=unseen;vector<string> river;
    for(auto d:player.river){++pool[norm(d.raw)];river.push_back(name(d.raw));}
    json base={{"pool",pool},{"river",river},{"per_stratum",retentionBaselineMode?16:512},{"bridge_samples",retentionBaselineMode?8:256}};
    if(constraintModel){base["current_constraint_key"]=constraintModel->signature;base["constraint_absolute_seat"]=constraintSeat;}
    string key=base.dump();auto found=actionCache.find(key);
    if(found==actionCache.end()) {
        static const json emissions=json::parse(R"EMISSIONS([[0.14166536389863987,0.2134040617938408,0.4850454073874047,0.15988516692011454],[0.16516613816829318,0.22341255274138547,0.4334796614139936,0.17794164767632775],[0.20475512772589946,0.240007407382005,0.35354542679894746,0.2016920380931481],[0.2579051741934678,0.25186892793914345,0.2799938692907766,0.21023202857661208],[0.32596074615984455,0.2602044831243869,0.2089334711208719,0.20490129959489678],[0.383049073576891,0.26040270473738963,0.17378374944152253,0.1827644722441969],[0.4279470356778412,0.25852205318267785,0.15413427119468123,0.15939663994479966],[0.45966408045726764,0.2607410562787563,0.13994351458968882,0.13965134867428722],[0.48729277306912255,0.2596457230222107,0.13530912566449954,0.11775237824416723],[0.5109175154102228,0.2601239558818568,0.13215517087552253,0.0968033578323978],[0.5252015728641225,0.2579576597688363,0.13340066614086546,0.08344010122617578],[0.5330534166290637,0.2574232401439192,0.13058339210878378,0.07893995111823326],[0.5335496371694497,0.251169330359811,0.13445333215902272,0.08082770031171661],[0.5405921754423053,0.23951103721998532,0.13967729225328435,0.08021949508442491],[0.5551387774670008,0.22473011811924687,0.13772468073112037,0.08240642368263196],[0.5756853992992063,0.20974734545500018,0.12858406554077076,0.08598318970502278],[0.5935975428682939,0.2032478937052969,0.11314175172448901,0.09001281170192005],[0.5956612882734088,0.20801677042307204,0.10862025155634608,0.08770168974717316],[0.58642378144831,0.22479634547009322,0.11014222062199018,0.07863765245960663],[0.5763528341272814,0.24578493817909333,0.11320451747578013,0.0646577102178451],[0.5718466491074042,0.250804799531753,0.11745195590674079,0.05989659545410203],[0.6261859582542694,0.2941176470588235,0.04554079696394687,0.03415559772296015],[0.6176470588235294,0.17647058823529413,0.11764705882352941,0.08823529411764706],[0.6176470588235294,0.17647058823529413,0.11764705882352941,0.08823529411764706],[0.6176470588235294,0.17647058823529413,0.11764705882352941,0.08823529411764706]])EMISSIONS");
        base["ordinary_emissions"]=emissions;
        array<uint64_t,3> seeds{{17,43,109}};array<json,3> results;array<double,3> evidence{};
        double common=-1e300;
        int seedCount=retentionBaselineMode?1:3;
        for(int i=0;i<seedCount;++i){checkActionDeadline();base["seed"]=seeds[i];results[i]=run(base,constraintModel,constraintSeat);evidence[i]=results[i].at("log_evidence");common=max(common,evidence[i]);}
        double total=0;RiskPosterior result;
        for(int i=0;i<seedCount;++i) {
            double w=exp(evidence[i]-common);total+=w;result.seedReady[i]=results[i].at("ready_probability");
            result.ready+=w*result.seedReady[i];result.pursuit+=w*results[i].at("posterior_pursuit").get<double>();
            for(int t=0;t<34;++t){result.ron[t]+=w*results[i]["ron_probability"][t].get<double>();result.doubleRon[t]+=w*results[i]["double_ron_probability"][t].get<double>();}
        }
        result.ready/=total;result.pursuit/=total;
        for(int t=0;t<34;++t){result.ron[t]/=total;result.doubleRon[t]/=total;}
        found=actionCache.emplace(key,result).first;
    }
    // Waiting sets, rather than independent tile guesses, determine furiten.
    auto result=found->second;bool thirteenBlocked=false;
    for(int t=0;t<34;++t)if(terminal(t) && player.safe[t])thirteenBlocked=true;
    for(int t=0;t<34;++t) {
        double single=max(0.,result.ron[t]-result.doubleRon[t]);
        result.doubleRon[t]=thirteenBlocked?0:result.doubleRon[t];
        result.ron[t]=(player.safe[t]?0:single)+result.doubleRon[t];
    }
    return result;
}
}
