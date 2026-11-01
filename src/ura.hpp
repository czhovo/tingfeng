#pragma once

namespace mj {
inline constexpr double IPPATSU_WIN_PRIOR=0.32000000000000001;
struct ExpectedBonuses {
    static constexpr int MAX_INDICATORS=5, MAX_URA=20, MAX_EXTRA=21;
    array<double,MAX_URA+1> ura{};
    array<double,MAX_EXTRA+1> extra{};
    array<int,5> groups{};
    int indicators=0,unknown=0;
    double ippatsu=0;
    static uint64_t inventoryKey(const array<int,5>& counts,int n) {
        if(n<0 || n>MAX_INDICATORS) throw runtime_error("invalid ura indicator count");
        uint64_t key=uint64_t(n)<<40;
        for(int i=0;i<5;++i) {
            if(counts[i]<0 || counts[i]>136) throw runtime_error("invalid ura inventory bin");
            key|=uint64_t(counts[i])<<(8*i);
        }
        return key;
    }
    static double choose(int n,int k) {
        if(k<0 || k>n) return 0;
        double out=1;
        for(int i=1;i<=k;++i) out*=double(n-i+1)/i;
        return out;
    }
    ExpectedBonuses(const array<int,5>& counts,int n,double ip=IPPATSU_WIN_PRIOR):groups(counts),indicators(n),ippatsu(ip) {
        inventoryKey(counts,n);
        unknown=accumulate(counts.begin(),counts.end(),0);
        if(unknown>136 || n>unknown || !isfinite(ip) || ip<0 || ip>1)
            throw runtime_error("invalid expected-bonus distribution input");
        if(n==0) ura[0]=1;
        else if(n==1) {
            for(int h=0;h<=4;++h) ura[h]=double(counts[h])/unknown;
        } else {
            // Each physical unknown tile is an equiprobable indicator. Group
            // by the number of matching dora in our complete winning hand.
            // Coefficients count subsets, so indicators are drawn WITHOUT replacement.
            array<array<double,MAX_URA+1>,MAX_INDICATORS+1> dp{},next{};
            dp[0][0]=1;
            for(int value=0;value<=4;++value) {
                next={}; array<double,MAX_INDICATORS+1> ways{};
                for(int take=0;take<=min(n,counts[value]);++take) ways[take]=choose(counts[value],take);
                for(int used=0;used<=n;++used) for(int han=0;han<=4*used;++han) if(dp[used][han]>0)
                    for(int take=0;take<=min(n-used,counts[value]);++take)
                        next[used+take][han+value*take]+=dp[used][han]*ways[take];
                dp=next;
            }
            double denominator=choose(unknown,n);
            for(int h=0;h<=4*n;++h) ura[h]=dp[n][h]/denominator;
        }
        double sum=accumulate(ura.begin(),ura.end(),0.0);
        if(!isfinite(sum) || abs(sum-1)>1e-10) throw runtime_error("ura probability normalization failed");
        for(int h=0;h<=MAX_URA;++h) {
            ura[h]/=sum;
            extra[h]+=(1-ip)*ura[h]; extra[h+1]+=ip*ura[h];
        }
    }
    json explain() const {
        double mean=0,tail=0;
        for(int h=0;h<=MAX_URA;++h) { mean+=h*ura[h]; if(h>=3) tail+=ura[h]; }
        return {{"indicator_value_bins",groups},{"unknown_physical_tiles",unknown},{"indicators",indicators},
                {"ura_probabilities",ura},{"extra_han_probabilities",extra},{"mean_ura",mean},
                {"ura_at_least_three",tail},{"ippatsu_given_win_prior",ippatsu}};
    }
};
struct BonusScoreKey {
    uint64_t shape=0,inventory=0;
    bool lastNormalTile=false;
    bool operator==(const BonusScoreKey& other) const {
        return shape==other.shape && inventory==other.inventory && lastNormalTile==other.lastNormalTile;
    }
};
struct BonusScoreHash {
    size_t operator()(const BonusScoreKey& key) const {
        uint64_t x=key.inventory+0x9e3779b97f4a7c15ULL;
        x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;
        return key.shape^(x^(x>>31))^(key.lastNormalTile?0x632be59bd9b4e019ULL:0);
    }
};
}
