#pragma once

namespace mj {
// All branches use the original public reference, even when a hypothetical
// declaration has deducted a new stick. Re-referencing each mask would erase
// the very score/rank movement that the forecast is intended to evaluate.
struct DeclarationValues {
    using Values=array<double,16>;
    int self=0;
    unsigned known=0,possible=0;
    double reference=0;
    vector<unsigned> masks;
    unordered_map<uint64_t,Values> ownCache;
    void reset(int who,unsigned current,unsigned mayDeclare) {
        self=who;known=current;possible=mayDeclare&~known&~(1u<<self);
        reference=PositionValue(self,known).reference();
        masks.clear();ownCache.clear();
        for(unsigned mask=0;mask<16;++mask)
            if((mask&known)==known && !(mask&~(known|possible))) masks.push_back(mask);
    }
    Values weights(const PublicHazardProjection& projected) const {
        Values out{};double total=0;
        for(unsigned mask:masks) {
            double p=1;
            for(int player=0;player<4;++player)
                p*=mask&(1u<<player)?projected.locked[player]:1-projected.locked[player];
            out[mask]=p;total+=p;
        }
        if(abs(total-1)>1e-9) throw runtime_error("future declaration masks do not conserve probability");
        return out;
    }
    double win(unsigned mask,int winner,int payer,int payment,bool tsumo,bool ownRiichi=false) const {
        PositionValue pos(self,mask);
        auto sc=pos.settled(winner,payer,payment,tsumo,ownRiichi);
        return standing.value(sc,standing.after(sc,winner),self)-reference;
    }
    const Values& ownWin(int payment,bool tsumo,bool ownRiichi,int payer=-1) {
        uint64_t key=(uint64_t(payment)<<8)|(unsigned(tsumo)<<6)|(unsigned(ownRiichi)<<5)|unsigned(payer+2);
        auto found=ownCache.find(key);if(found!=ownCache.end())return found->second;
        Values value{};
        for(unsigned mask:masks)value[mask]=win(mask,self,payer,payment,tsumo,ownRiichi);
        return ownCache.emplace(key,value).first->second;
    }
    double average(const Values& value,const Values& probability) const {
        double out=0;for(unsigned mask:masks)out+=probability[mask]*value[mask];return out;
    }
    double nagashi(bool selfReady,const PublicHazardProjection& projected) const {
        auto probability=weights(projected);double out=0;
        for(unsigned mask:masks)if(probability[mask]>0) {
            PositionValue pos(self,mask);unsigned required=mask;
            if(selfReady)required|=1u<<self;
            for(unsigned ready=0;ready<16;++ready) {
                if((ready&required)!=required || (!selfReady && !(mask&(1u<<self)) && (ready&(1u<<self))))continue;
                double p=probability[mask];
                for(int player=0;player<4;++player)if(player!=self && !(mask&(1u<<player)))
                    p*=ready&(1u<<player)?projected.silent[player]:1-projected.silent[player];
                if(p>0)out+=p*(pos.nagashiValue(ready)-reference);
            }
        }
        return out;
    }
    double draw(bool selfReady,bool ownRiichi,const PublicHazardProjection& projected) const {
        auto probability=weights(projected);double out=0;
        for(unsigned mask:masks) if(probability[mask]>0) {
            PositionValue pos(self,mask);
            unsigned required=mask;
            if(selfReady || ownRiichi)required|=1u<<self;
            for(unsigned ready=0;ready<16;++ready) {
                if((ready&required)!=required || (!selfReady && !ownRiichi && !(mask&(1u<<self)) && (ready&(1u<<self))))continue;
                double p=probability[mask];
                for(int player=0;player<4;++player) if(player!=self && !(mask&(1u<<player)))
                    p*=ready&(1u<<player)?projected.silent[player]:1-projected.silent[player];
                if(p>0)out+=p*(pos.drawValue(ready,ownRiichi)-reference);
            }
        }
        return out;
    }
};
}
