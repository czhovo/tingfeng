#pragma once

namespace mj {
struct OpponentPayouts {
    static constexpr int COUNT=21;
    // Exact standard ron payments, paired by the same underlying hand value.
    // Pooled shape from 28 development walls, balanced by wall/player-hand.
    // Details and model-label limitations: research/payout-distribution-002.
    array<int,COUNT> points;
    array<double,COUNT> probability{};
    explicit OpponentPayouts(double publicMean,bool dealer) {
        points=dealer?array<int,COUNT>{{1500,2000,2400,2900,3400,3900,4400,4800,5300,5800,6800,7700,8700,9600,10600,11600,12000,18000,24000,36000,48000}}:
                      array<int,COUNT>{{1000,1300,1600,2000,2300,2600,2900,3200,3600,3900,4500,5200,5800,6400,7100,7700,8000,12000,16000,24000,32000}};
        const array<double,COUNT> shape{{0.034249983220076094,0.13588752785313885,0.015333769205622752,0.12935097817382543,0.002441742654508612,0.16290809422279226,0.00095238095238095238,0.012372686511496497,0.00095238095238095238,0.1698549198118568,0.00095238095238095238,0.075237705078298578,0.00095238095238095238,0.0091703464172490975,0.00095238095238095238,0.11136521276332567,0.091967780638704089,0.033406695755826471,0.0063712568802706659,0.00095238095238095238,0.0043670150987224158}};
        double target=clamp(publicMean,double(points.front()),double(points.back()));
        array<double,COUNT> logs;
        for(int i=0;i<COUNT;++i) logs[i]=log(double(points[i])/points.front());
        auto distribution=[&](double tilt) {
            double total=0,mean=0,shift=tilt>0?tilt*logs.back():0;
            for(int i=0;i<COUNT;++i) {
                probability[i]=shape[i]*exp(tilt*logs[i]-shift);
                total+=probability[i];
            }
            for(int i=0;i<COUNT;++i) { probability[i]/=total; mean+=probability[i]*points[i]; }
            return mean;
        };
        // Power tilting changes spread around the old public expected amount;
        // it does not silently replace the old mean with a fitted larger mean.
        double lo=-80,hi=80;
        for(int k=0;k<56;++k) {
            double mid=(lo+hi)*.5;
            if(distribution(mid)<target) lo=mid; else hi=mid;
        }
        distribution((lo+hi)*.5);
    }
    double mean() const {
        double result=0;
        for(int i=0;i<COUNT;++i) result+=points[i]*probability[i];
        return result;
    }
    json explain() const {
        json branches=json::array();
        for(int i=0;i<COUNT;++i) branches.push_back({{"points",points[i]},{"probability",probability[i]}});
        return {{"branches",branches},{"mean_points",mean()}};
    }
};
}
