// Included inside chooseDiscard, after the complete parent-policy fallback.
// Progress estimates can fall with depth: refine ALL legal candidates
// together, never let an unsearched optimistic estimate win by default.
// Shape-only options are nonnegative refinements on a preselected beam.
#ifdef V63_SEARCH_DIAGNOSTIC
if(adaptiveEnabled)
#endif
if(deep && !ev.exhausted && cx.st.remaining>=6) {
    auto ranked=[&](int cap,bool sameSh) {
        vector<int> order;double top=-1e30;
        for(const auto& c:cs)top=max(top,searchUtility(c));
        for(int i=0;i<(int)cs.size();++i)
            if((!sameSh || cs[i].sh==minsh) && searchUtility(cs[i])+.65*retentionUnit()>=top)order.push_back(i);
        stable_sort(order.begin(),order.end(),[&](int a,int b){return searchUtility(cs[a])>searchUtility(cs[b]);});
        if((int)order.size()>cap)order.resize(cap);
        return order;
    };
    auto refine=[&](const vector<int>& selected,int progress,int shape) {
        if(selected.empty() || ev.budgetExpired())return false;
        bool complete=search.refine(cs,selected,progress,shape);
        return complete;
    };
    // Far hands used to stop at one/two progress layers even with spare time.
    vector<int> beam(cs.size());iota(beam.begin(),beam.end(),0);
    for(int depth=targetDepth+1;depth<=min(4,maxsh) && !ev.exhausted;++depth) {
#ifdef PARALLEL_REGRESSION
        if(depth>parallelTestDepth)break;
#endif
        bool useful=false;for(int i:beam)useful|=cs[i].depth<min(depth,cs[i].sh);
        if(!useful)break;
        if(!refine(beam,depth,0))break;
    }
    if(minsh<=2 && !ev.exhausted
#ifdef PARALLEL_REGRESSION
       && !parallelTestNoShape
#endif
    ) {
        // Finish first-order shape search for every competitive min-shanten
        // discard, including alternatives excluded by the old top-three cap.
        auto wide=ranked(34,true);
        if(refine(wide,0,1) && cx.st.remaining>=10) {
            // Two successive same-shanten changes, with separate depleted
            // tiles and furiten histories. No repeated pass over equal work.
            auto beam2=ranked(3,true);
            refine(beam2,0,2);
        }
    }
}
