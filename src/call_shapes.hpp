// Included inside namespace mj, after Hand/Meld/Future are defined.
struct FutureCallShape {
    int tile=-1,a=-1,b=-1,discard=-1;
    bool pon=false;
    Hand after;
};
inline vector<FutureCallShape> futureCallShapes(const Hand& h,int tile,int melds) {
    vector<FutureCallShape> out;
    int sh=shanten(h,melds);
    if(sh<=0 || melds>=4)return out;
    vector<pair<int,int>> pairs;
    if(h.c[tile]>=2)pairs.push_back({tile,tile});
    if(tile<27)for(int begin=max(tile/9*9,tile-2);begin<=min(tile,tile/9*9+6);++begin) {
        vector<int> need;
        for(int t=begin;t<begin+3;++t)if(t!=tile)need.push_back(t);
        if(h.c[need[0]] && h.c[need[1]])pairs.push_back({need[0],need[1]});
    }
    for(auto [a,b]:pairs) {
        Hand next=h;next.add(a,-1);next.add(b,-1);
        int begin=min({a,b,tile});bool pon=a==b;
        for(int d=0;d<34;++d)if(next.c[d]) {
            if(d==tile || (!pon && d/9==tile/9 && ((tile==begin && d==tile+3) || (tile==begin+2 && d==tile-3))))continue;
            next.add(d,-1);
            if(shanten(next,melds+1)==sh-1)out.push_back({tile,a,b,d,pon,next});
            next.add(d);
        }
    }
    return out;
}
struct CallContinuation {
    static constexpr int WALL=76;
    int tile=-1;
    bool pon=false;
    double copies=0;
    array<double,WALL> value{},win{},dealIn{};
    CallContinuation(){value.fill(-1e30);}
};
