// Included in namespace mj after deadlineResponse/deadlineSnapshot declarations.
inline double retentionUnit(){return standing.enabled?max(1.,standing.returnPoints/25.):1000.;}
inline thread_local bool retentionBaselineMode=false;
#ifdef RETENTION_FAULT_TEST
inline thread_local int retentionFaultMode=0,retentionFaultCounter=0;
inline void retentionFaultPoint(int phase){
    if((phase==1 && retentionFaultMode==1) || (phase==2 && !retentionBaselineMode && retentionFaultMode==2 && ++retentionFaultCounter==1))
        prt::request.deadline=prt::Clock::now()-chrono::milliseconds(1);
}
#endif
struct RetentionAlternative {
    string action,kind="attack",tileName,probabilityBasis="computed_continuation";
    string riskBasis="modeled_current_hand";
    int maximumOwnDraws=-1,origin=-1,raw=-1,sh=9,uke=0,depth=0;
    double base=0,off=0,conditionalWin=0,conditionalRisk=0,survival=1,loss=0,switchCost=0;
    double shapeAdjustment=0,redAdjustment=0,shantenAdjustment=0,reserveAdjustment=0,closedAdjustment=0;
    bool known=true,riskKnown=true,immediateRiskKnown=true,complete=true,riichi=false,hasImmediateDiscard=false;
    string effectiveRiskBasis()const{return !standing.enabled && riskBasis=="modeled_current_hand"?"current_discard_only_legacy":riskBasis;}
    retention::Entry entry()const{
        const auto riskScope=effectiveRiskBasis();
        double risk=riskScope=="current_discard_only_legacy"?1-survival:1-survival+survival*conditionalRisk;
        bool rk=immediateRiskKnown && (riskKnown || riskScope=="current_discard_only_legacy");
        return {base-switchCost,survival*conditionalWin,known,complete,risk,rk};
    }
    json explain(double unit)const{
        auto e=entry();const bool wk=e.validWin(),rk=e.validRisk();const auto riskScope=effectiveRiskBasis();
        double baseSurvival=standing.enabled?survival:1.;
        double adjustments=shapeAdjustment+redAdjustment+shantenAdjustment+reserveAdjustment+closedAdjustment;
        double currentPenalty=immediateRiskKnown?retention::DEAL_IN_UNITS*unit*(1-survival):0;
        double futurePenalty=riskKnown && riskScope!="current_discard_only_legacy"?retention::DEAL_IN_UNITS*unit*survival*conditionalRisk:0;
        return {{"action",action},{"policy",kind},{"tile",tileName},{"origin",origin},
            {"original_candidate_score",base},{"switch_cost",switchCost},{"base_score",e.base},{"off",off},
            {"base_components",{{"conditional_continuation",off},{"current_survival_multiplier",baseSurvival},
                {"continuation_lost_on_current_ron",(1-baseSurvival)*off},{"current_expected_loss",loss},
                {"shape_adjustment",shapeAdjustment},{"red_discard_adjustment",redAdjustment},{"shanten_adjustment",shantenAdjustment},
                {"reserve_adjustment",reserveAdjustment},{"closed_hand_adjustment",closedAdjustment},{"switch_cost",switchCost},
                {"remaining_base_adjustment",base-(baseSurvival*off-loss+adjustments)}}},
            {"win",wk?json(conditionalWin):json(nullptr)},{"win_after_current_risk",wk?json(e.win):json(nullptr)},
            {"current_p_ron",hasImmediateDiscard && immediateRiskKnown?json(1-survival):json(nullptr)},
            {"current_loss",loss},{"future_deal_in_conditional",riskKnown && riskScope!="current_discard_only_legacy"?json(conditionalRisk):json(nullptr)},
            {"forecast_deal_in",rk?json(e.dealIn):json(nullptr)},{"risk_probability_scope",riskScope},
            {"win_completion_utility",wk?json(retention::winReward(e,unit)):json(nullptr)},
            {"deal_in_preference_cost",rk?json(retention::riskPenalty(e,unit)):json(nullptr)},
            {"current_deal_in_preference_cost",immediateRiskKnown?json(currentPenalty):json(nullptr)},
            {"future_deal_in_preference_cost",riskKnown && riskScope!="current_discard_only_legacy"?json(futurePenalty):json(nullptr)},
            {"win_utility_lower_bound",retention::winReward(e,unit)},
            {"risk_cost_upper_bound",retention::riskPenalty(e,unit)},
            {"unknown_risk_reserve",rk?0:max(0.,retention::riskPenalty(e,unit)-currentPenalty-futurePenalty)},
            {"score",retention::objective(e,unit)},{"score_is_lower_bound",!wk || !rk},
            {"probability_status",wk?"estimated":"not_computed"},{"probability_basis",probabilityBasis},
            {"maximum_own_draws",maximumOwnDraws},{"complete",complete},{"usable",e.validBase()},
            {"riichi",riichi},{"sh",sh},{"uke",uke},{"depth",depth}};
    }
};
struct RetentionDecision {
    vector<RetentionAlternative> alternatives;vector<retention::Entry> entries;
    int selected=-1;double unit=1000;string scope,resolution;
    RetentionDecision()=default;
    RetentionDecision(vector<RetentionAlternative> choices,string where):alternatives(move(choices)),unit(retentionUnit()),scope(move(where)),
        resolution(retentionBaselineMode?"complete_fast_model_baseline":"complete_full_model_pass"){
        for(const auto& x:alternatives)entries.push_back(x.entry());selected=retention::select(entries,unit);
    }
    bool valid()const{return selected>=0 && selected<int(alternatives.size()) && entries[selected].validBase();}
    const RetentionAlternative& chosen()const{return alternatives.at(selected);}
    int representative(int origin)const{
        if(valid() && chosen().origin==origin)return selected;
        int best=-1;
        for(int i=0;i<int(alternatives.size());++i)if(alternatives[i].origin==origin && entries[i].validBase())
            if(best<0 || retention::objective(entries[i],unit)>retention::objective(entries[best],unit))best=i;
        return best;
    }
    json explain()const{
        json rows=json::array();
        for(int i=0;i<int(alternatives.size());++i){auto row=alternatives[i].explain(unit);row["index"]=i;row["selected"]=i==selected;rows.push_back(move(row));}
        return {{"retention_decision",true},{"valuation_revision","continuous_win_and_deal_in_event_utility"},
            {"scope",scope},{"resolution",resolution},{"complete_candidate_pass",true},{"hard_filter",false},
            {"unit",unit},{"own_win_completion_reward",retention::WIN_UNITS*unit},{"deal_in_event_cost",retention::DEAL_IN_UNITS*unit},
            {"selection_rule","maximum continuous utility; explicit lower bound for unknown components"},
            {"action",valid()?json(chosen().action):json(nullptr)},{"selected_index",selected},{"candidates",rows}};
    }
};
inline thread_local RetentionDecision retainedDecision;
inline bool rememberRetention(RetentionDecision decision){
    if(!decision.valid())return false;
    retainedDecision=move(decision);const auto& a=retainedDecision.chosen();auto e=a.entry();deadlineResponse=a.action;
    deadlineSnapshot={{"source","last_complete_soft_pass"},{"scope",retainedDecision.scope},{"resolution",retainedDecision.resolution},
        {"off",a.off},{"base_score",e.base},{"score",retention::objective(e,retainedDecision.unit)},
        {"win",e.validWin()?json(a.conditionalWin):json(nullptr)},{"win_after_current_risk",e.validWin()?json(e.win):json(nullptr)},
        {"forecast_deal_in",e.validRisk()?json(e.dealIn):json(nullptr)},{"complete_candidate_set",true},{"hard_filter",false}};
    return true;
}
inline void traceRetentionFinal(const string& response){
    if(!debugPolicy)return;
    if(retainedDecision.valid() && retainedDecision.chosen().action==response){auto j=retainedDecision.explain();j["final_action"]=response;cerr<<j.dump()<<'\n';}
    else cerr<<json({{"retention_decision",true},{"final_action",response},{"status","terminal_or_no_comparable_alternative"},
        {"hard_filter",false},{"win_after_current_risk",response=="HU"?json(1.):json(nullptr)}}).dump()<<'\n';
}
