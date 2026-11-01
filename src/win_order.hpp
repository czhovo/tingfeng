#pragma once
namespace mj {
struct WinScore;
struct WinOffer {
    double probability=0,gain=0;
    void scale(double factor){probability*=factor;gain*=factor;}
};
struct OrderedWinInterval {
    const DeclarationValues* payouts=nullptr;
    int opponents=0,elapsed=0,ownElapsed=0;
    bool ownDraw=false,committed=false;
    array<int,3> actors{};
    array<double,4> normalSources{},extraSources{};
    array<TemporalOtherWins::OrderedStep,3> other{};
    DeclarationValues::Values extraWeights{},ownWeights{};
    array<DeclarationValues::Values,3> noTsumoWeights{};
    WinOffer extra,tsumo;
    array<WinOffer,3> ron{};
    void addTsumo(double probability,const WinScore& score);
    void addRon(double probability,const WinScore& ordinary,const WinScore& last,bool lastOrdinary,const array<double,4>* release=nullptr);
    void cap() {
        double total=extra.probability+tsumo.probability;
        for(int i=0;i<opponents;++i)total+=ron[i].probability;
        if(total>.65) {
            double scale=.65/total;extra.scale(scale);tsumo.scale(scale);
            for(auto& offer:ron)offer.scale(scale);
        }
    }
    retention::Value settle(double continuation,double continuingWin,double discardTax=0,double discardSurvival=1,double continuingRisk=0) const {
        double value=continuation,win=continuingWin,risk=continuingRisk;
        // The cost belongs to a discard AFTER a failed own draw, not to
        // branches on which somebody ended the hand before we could draw.
        if(ownDraw) {
            value=tsumo.gain+(1-tsumo.probability)*(discardSurvival*value-discardTax);
            win=tsumo.probability+(1-tsumo.probability)*discardSurvival*win;
            risk=(1-tsumo.probability)*(1-discardSurvival+discardSurvival*risk);
        }
        for(int i=opponents-1;i>=0;--i) {
            const auto& event=other[i];const auto& offer=ron[i];int branch=committed?1:0;
            value=event.tsumoStopped[branch]+offer.gain+
                (1-offer.probability)*(event.ronStopped[branch]+event.survival*value);
            risk*=(1-offer.probability)*event.survival;
            win=event.tsumoSurvival*offer.probability+
                (1-offer.probability)*event.survival*win;
        }
        // A small pooled call-discard opportunity is placed before the next
        // normal draw. It never receives houtei and is not placed after an
        // already-terminal last draw. Its frequency remains approximate.
        value=extra.gain+(1-extra.probability)*value;
        win=extra.probability+(1-extra.probability)*win;
        return {value,win,(1-extra.probability)*risk};
    }
};
}
