// Included inside Evaluator. All conditioning is against the current snapshot;
// futureWallDraws records only hypothetical samples from the initial wall.
Hand futureWallDraws;
mutable ohc::Counts constraintLastAcquired{},constraintLastWall{};
mutable bool constraintViewValid=false;
mutable ohc::Distribution constraintView;
const ohc::Distribution& currentConstraintView()const{
    if(!cx.constraintModel)throw logic_error("constraint view without current model");
    if(!constraintViewValid || constraintLastAcquired!=futureDraws.c || constraintLastWall!=futureWallDraws.c){
        array<bool,4> locked{};for(int p=0;p<4;++p)locked[p]=cx.st.pl[p].riichi;
        try{constraintView=cx.constraintModel->distribution(futureDraws.c,futureWallDraws.c,locked);}
        catch(const ohc::BudgetExpired&){throw ActionBudgetExpired{};}
        if(!constraintView.possible)throw logic_error("zero-support hypothetical wall prefix");
        constraintLastAcquired=futureDraws.c;constraintLastWall=futureWallDraws.c;constraintViewValid=true;
    }
    return constraintView;
}
double conditionedDrawRate(int t)const{
    const auto& view=currentConstraintView();
    // Preserve the parent's effective-pool/horizon convention. Conditioning
    // changes a tile's relative source probability, not visible tile counts.
    return view.wall[t]*view.remainingUnknown/pool;
}
array<double,4> conditionedReleaseRatios(int t)const{
    const auto& view=currentConstraintView();array<double,4> ratios{};
    int copies=max(0,cx.unseen[t]-futureDraws.c[t]);
    if(!copies || !view.remainingUnknown)return ratios;
    double prior=double(copies)/view.remainingUnknown;
    for(int p=0;p<4;++p)if(p!=cx.st.seat)ratios[p]=view.release[p][t]/prior;
    return ratios;
}
double conditionedGenericRate(const Hand& h,int t,int copies)const{
    double rate=conditionedDrawRate(t),extra=0;bool fromAll=true;
    if(cx.st.closed())return rate;
    if(!cfg.call)extra=.13;
    else{bool pon=false,chi=false;for(const auto& x:futureCallShapes(h,t,cx.melds)){pon|=x.pon;chi|=!x.pon;}
        if(pon)extra=cfg.pon-1;else if(chi){extra=cfg.chi-1;fromAll=false;}}
    if(extra<=0)return rate;auto release=conditionedReleaseRatios(t);double factor=0;
    if(fromAll){for(int p=0;p<4;++p)if(p!=cx.st.seat)factor+=release[p]/3.;}
    else factor=release[(cx.st.seat+3)%4];
    return rate+copies/pool*extra*factor;
}
