// Included after Choice; no evaluation occurs while committing a candidate set.
inline string retentionDiscardAction(const Choice& c,const Context& cx,bool forcePlay=false){
    bool allowed=false;
    for(auto part:split(deadlineValidActions,',')){auto w=words(part);if(w.empty()||w[0]!="LIZHI")continue;
        if(w.size()==1)allowed=true;for(size_t i=1;i<w.size();++i)if(tile(w[i])==c.raw)allowed=true;}
    return string(!forcePlay && allowed && c.riichi && canDeclareRiichi(cx.st)?"LIZHI ":"PLAY ")+name(c.raw);
}
inline RetentionDecision discardDecision(const vector<Choice>& choices,const Context& cx){
    vector<RetentionAlternative> out;
    for(int i=0;i<int(choices.size());++i){const auto& c=choices[i];int t=norm(c.raw);
        RetentionAlternative a;a.action=retentionDiscardAction(c,cx);a.kind="attack";a.tileName=name(c.raw);a.origin=i;a.raw=c.raw;
        a.sh=c.sh;a.uke=c.uke;a.depth=c.depth;a.base=c.score;a.off=c.offense;a.conditionalWin=c.win;a.known=c.winAvailable;a.conditionalRisk=c.dealIn;a.riskBasis=standing.enabled?"modeled_current_hand":"current_discard_only_legacy";
        a.shapeAdjustment=c.shapeAdjustment;a.redAdjustment=c.redAdjustment;a.shantenAdjustment=c.shantenAdjustment;a.reserveAdjustment=c.reserveAdjustment;
        a.survival=cx.immediateSurvival[t];a.loss=cx.danger[t];a.riichi=c.riichi;a.hasImmediateDiscard=true;a.complete=c.estimateComplete;
        bool alreadyFold=c.defenseAvailable && abs(c.offense-c.defenseOff)<1e-9 && c.win==0 && !c.riichi;
        if(!alreadyFold)out.push_back(a);
        if(c.defenseAvailable){auto f=a;f.action=retentionDiscardAction(c,cx,true);f.kind="defense";f.base=c.defenseScore;f.off=c.defenseOff;
            f.conditionalWin=0;f.conditionalRisk=c.defenseRisk;f.shapeAdjustment=f.redAdjustment=f.shantenAdjustment=0;f.known=true;f.riichi=false;out.push_back(f);}
        if(c.nagashiOff>-1e20){auto n=a;n.action=retentionDiscardAction(c,cx,true);n.kind="nagashi";n.base=cx.actionValue(t,c.nagashiOff);n.off=c.nagashiOff;
            n.riskKnown=false;n.conditionalRisk=std::numeric_limits<double>::quiet_NaN();n.riskBasis="nagashi_future_exposure_not_computed";
            n.shapeAdjustment=n.redAdjustment=n.shantenAdjustment=n.reserveAdjustment=0;n.known=c.nagashiZeroWinCertified;n.conditionalWin=n.known?0:std::numeric_limits<double>::quiet_NaN();n.riichi=false;
            n.maximumOwnDraws=c.nagashiOwnDraws;n.probabilityBasis=n.known?"nagashi_no_future_calls_shanten_exceeds_own_draws":"nagashi_normal_win_not_computed";out.push_back(n);}
    }
    return RetentionDecision(move(out),"discard");
}
inline void rememberDiscard(const vector<Choice>& choices,const Context& cx){rememberRetention(discardDecision(choices,cx));}
inline Choice retainedDiscardChoice(){
    if(!retainedDecision.valid())throw runtime_error("retention: no complete valid discard candidate set");
    const auto& a=retainedDecision.chosen();Choice c;c.raw=a.raw;c.sh=a.sh;c.uke=a.uke;c.depth=a.depth;c.score=a.base;
    c.offense=a.off;c.win=a.conditionalWin;c.dealIn=a.conditionalRisk;c.winAvailable=a.known;c.riichi=a.riichi;c.estimateComplete=true;return c;
}
inline void traceDiscardProbabilities(const RetentionDecision& decision,int horizon,int tailDraws){
    if(!debugPolicy || !decision.valid())return;json rows=json::array();
    vector<int> origins;for(const auto& a:decision.alternatives)if(find(origins.begin(),origins.end(),a.origin)==origins.end())origins.push_back(a.origin);
    for(int origin:origins){int best=decision.representative(origin);if(best<0)continue;
        auto row=decision.alternatives[best].explain(decision.unit);row["selected"]=best==decision.selected;
        row["win_status"]=decision.entries[best].validWin()?"estimated_policy_current_hand_conditional_on_current_discard_passing":"policy_win_not_computed";
        rows.push_back(move(row));}
    cerr<<json({{"decision_probabilities","discard"},{"own_draw_horizon",horizon},{"tail_draws",tailDraws},{"candidates",rows}}).dump()<<'\n';
}
