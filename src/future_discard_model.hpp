// Included after Context, inside namespace mj.
inline void Context::buildFutureExposure() {
    if(!standing.enabled)return;
    for(int elapsed=0;elapsed<TemporalOtherWins::MAX_TIME;++elapsed) {
        auto projected=otherWins.project(elapsed);
        const auto& weights=declarationWeights[elapsed];
        for(auto& x:futureDrawExposure[elapsed]){x.survival=0;x.loss=0;}
        if(totalUnknown<=0) {for(auto& x:futureDrawExposure[elapsed])x.survival=1;continue;}
        for(int tile=0;tile<34;++tile)if(unseen[tile]>0) {
            double tileWeight=constraintModel?constraintWallProbability[tile]:double(unseen[tile])/totalUnknown;
            for(unsigned mask:declarations.masks)if(weights[mask]>0) {
                double survive=1;array<double,2> loss{};
                for(int who=0;who<4;++who)if(who!=st.seat) {
                    double ready=mask&(1u<<who)?1:projected.silent[who];
                    double ordinary=ronShape[who][tile]*ready;
                    double twice=specialDoubleRon[who][tile];
                    double single=max(0.,specialRon[who][tile]-twice);
                    double probability=clamp(ordinary+single+twice,0.,1.);
                    survive*=1-probability;
                    for(int committed=0;committed<2;++committed) {
                        // Payout and risk use the SAME projected declaration
                        // mask, including accepted sticks and actual point
                        // thresholds. The self-payer column was unused by
                        // other-player stopping events (actor==self returns).
                        double gain=ordinary*otherWins.ronValue[mask][who][st.seat][committed]+
                            single*specialSingleValue[mask][who][committed]+
                            twice*specialDoubleValue[mask][who][committed];
                        loss[committed]-=cfg.risk*gain;
                    }
                }
                double weight=tileWeight*weights[mask];
                for(int committed=0;committed<2;++committed) {
                    futureDrawExposure[elapsed][committed].survival+=weight*survive;
                    // Keep the existing nonnegative coarse future-loss
                    // convention. Immediate discard values remain signed.
                    futureDrawExposure[elapsed][committed].loss+=weight*max(0.,loss[committed]);
                }
            }
        }
        for(auto& x:futureDrawExposure[elapsed])x.survival=clamp(x.survival,0.,1.);
    }
    if(debugPolicy) {
        json times=json::array();
        for(int elapsed:{0,4,12,24,40})if(elapsed<=st.remaining) {
            const auto& f=futureDrawExposure[elapsed];auto p=otherWins.project(elapsed);
            times.push_back({{"elapsed",elapsed},{"survival",f[0].survival},
                {"loss_dama",f[0].loss},{"loss_committed",f[1].loss},
                {"locked",p.locked},{"silent_ready",p.silent}});
        }
        cerr<<json({{"future_discard_model",true},{"current_mean_danger",meanDanger},
            {"source","existing public progress/declaration parameters; no risk refit"},{"times",times}}).dump()<<'\n';
    }
}
