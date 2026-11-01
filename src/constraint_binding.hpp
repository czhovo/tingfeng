// Included after State. The source owns historical maintenance of the bounds.
// This function is called only AFTER State::consume, once for every request.
inline void bindCurrentOpponentConstraints(State& state,const json& input){
    state.opponentConstraints=ohc::Snapshot::parse(input,state.seat,[]{return sharedSearchStopped();});
    array<int,34> visible=state.publicSeen,unknown{};bool consistent=true;
    for(int t:state.indicators)if(t>=0)++visible[norm(t)];
    for(int t=0;t<34;++t){visible[t]+=state.hand.c[t];consistent&=visible[t]>=0&&visible[t]<=4&&state.hand.c[t]>=0&&state.hand.c[t]<=4;
        unknown[t]=max(0,4-visible[t]);}
    array<int,4> capacities{};
    for(int p=0;p<4;++p)capacities[p]=13-3*int(state.pl[p].melds.size())+
        int(state.event=="DRAW"&&state.actor==p);
    consistent&=state.seat>=0&&state.seat<4;
    if(consistent)consistent=state.hand.n==capacities[state.seat];
    auto& snapshot=*state.opponentConstraints;
    snapshot.meta["engine_inventory_reference"]={{"self",state.seat},{"round_wind",state.round},
        {"opponent_concealed_capacities",capacities},{"own_hand_count",state.hand.n},
        {"unknown_total",accumulate(unknown.begin(),unknown.end(),0)},{"inventory_consistent",consistent}};
    snapshot.resolve(unknown,capacities,consistent);
}
inline void recordOpponentConstraintTelemetry(const State& state){
    if(!state.opponentConstraints)return;
    auto snapshot=state.opponentConstraints;
    dt::constraintInfo=snapshot->summary();
    if(debugPolicy)dt::constraintDetails=[snapshot]{auto out=snapshot->analysis();out["type"]="opponent_hand_constraints_analysis";return out;};
}
