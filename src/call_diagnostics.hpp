// Included inside namespace mj, after Context and CallOption are defined.
// These helpers only serialize values already computed by the policy. They
// never construct an evaluator, update a model, or select an action.
inline void appendDiscardDiagnostic(json& row,const Context* cx,int raw) {
    row["off_scope"]="continuation_utility";
    row["p_ron_scope"]="immediate_discard";
    if(raw<0) {
        row["p_ron"]=nullptr;row["p_ron_status"]="not_applicable";
        row["p_ron_reason"]="no_immediate_own_discard";
    } else if(cx) {
        row["p_ron"]=1-cx->immediateSurvival[norm(raw)];
        row["p_ron_status"]="estimated";
    } else {
        row["p_ron"]=nullptr;row["p_ron_status"]="not_estimated";
        row["p_ron_reason"]="context_not_evaluated";
    }
}

inline void traceUnestimatedCall(const string& action,const string& reason,
                                 const Context* cx=nullptr,int raw=-1,int group=-1) {
    if(!debugPolicy)return;
    json row={{"call_unestimated",true},{"action",action},{"reason",reason},
              {"off",nullptr},{"off_status","not_evaluated"}};
    if(raw>=0)row["tile"]=name(raw);
    if(group>=0)row["group"]=group;
    appendDiscardDiagnostic(row,cx,raw);
    cerr<<row.dump()<<'\n';
}

inline void appendKanDiagnostic(json& row,const Context* cx,const string& kind,int raw) {
    row["off_scope"]="continuation_utility";
    if(kind=="BUGANG") {
        row["p_ron_scope"]="added_kan_robbery";
        row["p_ron_model"]="ordinary_tile_model_proxy";
        if(cx && raw>=0) {
            // This is exactly the probability already used by actionValue
            // for the added kan. It is NOT a separate chankan inference.
            row["p_ron"]=1-cx->immediateSurvival[norm(raw)];
            row["p_ron_status"]="proxy";
        } else {
            row["p_ron"]=nullptr;row["p_ron_status"]="not_estimated";
            row["p_ron_reason"]="context_not_evaluated";
        }
    } else {
        row["p_ron_scope"]="concealed_kan_robbery";
        row["p_ron"]=nullptr;row["p_ron_status"]="not_modelled";
        row["p_ron_reason"]="concealed_kan_robbery_not_modelled";
    }
}

inline void traceUnestimatedKan(const string& kind,int raw,const string& reason,
                                const Context* cx=nullptr) {
    if(!debugPolicy)return;
    json row={{"kan_unestimated",true},{"kan",kind},{"tile",raw<0?json(nullptr):json(name(raw))},
              {"reason",reason},{"off",nullptr},{"off_status","not_evaluated"}};
    appendKanDiagnostic(row,cx,kind,raw);
    cerr<<row.dump()<<'\n';
}

inline void traceUnestimatedKanOffers(const string& valid,const string& reason) {
    if(!debugPolicy)return;
    for(const auto& part:split(valid,',')) {
        auto tokens=words(part);
        if(!tokens.empty() && (tokens[0]=="ANGANG" || tokens[0]=="BUGANG"))
            traceUnestimatedKan(tokens[0],-1,reason);
    }
}

inline void traceComputedKan(const string& kind,int raw,const Context& cx,double off,
                              double value,double normal,const string& status="evaluated",
                              const string& reason="") {
    if(!debugPolicy)return;
    json row={{"kan",kind},{"tile",name(raw)},{"value",value},{"normal",normal},
              {"off",off},{"off_status","evaluated"},{"candidate_status",status}};
    if(!reason.empty())row["reason"]=reason;
    appendKanDiagnostic(row,&cx,kind,raw);
    cerr<<row.dump()<<'\n';
}
