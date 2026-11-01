// PASS participates in each extra layer, so a deeper call is never compared
// only against its stale PASS valuation. The original result remains saved.
#ifdef V63_SEARCH_DIAGNOSTIC
if(adaptiveEnabled)
#endif
if(mainComplete && st.remaining>=10 && !timedOut()) {
    double top=searchUtility(choices[0]);for(const auto& c:choices)top=max(top,searchUtility(c));
    vector<int> selected;
    for(int i=1;i<(int)choices.size();++i)
        if(searchUtility(choices[i])+.65*retentionUnit()>=top)selected.push_back(i);
    stable_sort(selected.begin(),selected.end(),[&](int a,int b){return searchUtility(choices[a])>searchUtility(choices[b]);});
    if(selected.size()>3)selected.resize(3);selected.insert(selected.begin(),0);
    auto refine=[&](const vector<int>& selected,int progress,int shape) {
        bool complete=parallelRefine(selected,progress,shape);
        return complete;
    };
    vector<int> all(choices.size());iota(all.begin(),all.end(),0);
    bool ok=true;
    for(int depth=target+1;depth<=min(4,maxsh) && ok;++depth) {
#ifdef PARALLEL_REGRESSION
        if(depth>parallelTestDepth)break;
#endif
        bool useful=false;for(int i:all)useful|=choices[i].depth<min(depth,choices[i].sh);
        if(!useful)break;ok=refine(all,depth,0);
    }
    if(ok && oldsh<=2 && !timedOut()
#ifdef PARALLEL_REGRESSION
       && !parallelTestNoShape
#endif
    )refine(selected,0,2);
}
