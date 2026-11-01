#pragma once

namespace mj {
struct PublicHazardPlayer {
    bool locked=false,open=false,canDeclare=false;
    int turn=0,melds=0;
    double ready=0;
};
struct PublicHazardProjection {
    array<double,4> silent{},locked{};
    array<int,4> completed{},eligible{};
    json explain() const {
        return {{"silent_pressure",silent},{"locked_probability",locked},
                {"completed_normal_turns",completed},{"eligible_declaration_turns",eligible}};
    }
};
struct TemporalOtherWins {
    static constexpr int MAX_TIME=72;
    static constexpr double TSUMO_UNLOCKED=.0345,TSUMO_LOCKED=.1074;
    static constexpr double RON_UNLOCKED=.0213,RON_LOCKED=.0900;
    static constexpr double DECLARATION_RATE=.735,PROGRESS_RATE=.024;
    struct Cell {
        double survival=1;array<double,2> stopped{};
        double tsumoSurvival=1,silentTsumoSurvival=1,lockedTsumoSurvival=1;
        array<double,2> tsumoStopped{};
    };
    struct OrderedStep {
        double survival=1,tsumoSurvival=1,scale=1;
        array<double,2> tsumoStopped{},ronStopped{};
    };
    array<PublicHazardPlayer,4> players{};
    array<array<array<double,2>,4>,16> tsumoValue{};
    array<array<array<array<double,2>,4>,4>,16> ronValue{};
    array<array<Cell,4>,MAX_TIME> timeline{};
    int self=0,rootActor=0,remaining=0;
    bool replacement=false;
    double fullSurvival=1;
    static double silentAtTurn(const PublicHazardPlayer& p,int turn) {
        if(p.melds==0) return .008+.0015*turn+.00019*turn*turn;
        const double intercept[]={0,-3.65,-2.45,-1.1,-.3};
        return 1/(1+exp(-(intercept[min(4,p.melds)]+.23*turn)));
    }
    static int turnsThrough(int afterActor,int who,int draws) {
        int distance=(who-afterActor+4)%4;if(!distance)distance=4;
        return draws>=distance?1+(draws-distance)/4:0;
    }
    PublicHazardProjection projectCounts(const array<int,4>& completed,const array<int,4>& eligible) const {
        PublicHazardProjection out;out.completed=completed;out.eligible=eligible;
        for(int p=0;p<4;++p) {
            const auto& state=players[p];
            double base=clamp(max(state.ready,silentAtTurn(state,state.turn+completed[p])),0.,1.);
            out.silent[p]=1-(1-base)*exp(-PROGRESS_RATE*completed[p]);
            if(state.locked) out.locked[p]=1;
            else if(!state.open && state.canDeclare) {
                double exposure=0;
                for(int j=0;j<eligible[p];++j)
                    exposure+=max(state.ready,silentAtTurn(state,state.turn+j));
                out.locked[p]=-expm1(-DECLARATION_RATE*exposure);
            }
        }
        return out;
    }
    PublicHazardProjection project(int elapsed) const {
        int normal=max(0,elapsed-(replacement?1:0));
        int legal=min(normal,max(0,remaining-4-(replacement?1:0)));
        array<int,4> completed{},eligible{};
        for(int p=0;p<4;++p) {
            completed[p]=turnsThrough(rootActor,p,normal);
            eligible[p]=turnsThrough(rootActor,p,legal);
        }
        return projectCounts(completed,eligible);
    }
    void addTsumo(int winner,double mass,const array<double,2>& value) {
        for(unsigned mask=0;mask<16;++mask)addTsumo(mask,winner,mass,value);
    }
    void addRon(int winner,int payer,double mass,const array<double,2>& value) {
        for(unsigned mask=0;mask<16;++mask)addRon(mask,winner,payer,mass,value);
    }
    void addTsumo(unsigned mask,int winner,double mass,const array<double,2>& value) {
        for(int b=0;b<2;++b) tsumoValue[mask][winner][b]+=mass*value[b];
    }
    void addRon(unsigned mask,int winner,int payer,double mass,const array<double,2>& value) {
        for(int b=0;b<2;++b) ronValue[mask][winner][payer][b]+=mass*value[b];
    }
    Cell atProjection(int actor,const PublicHazardProjection& q) const {
        Cell out;if(actor==self)return out;out.survival=out.tsumoSurvival=0;
        out.silentTsumoSurvival=exp(-TSUMO_UNLOCKED*q.silent[actor]);
        out.lockedTsumoSurvival=exp(-TSUMO_LOCKED);
        // Sum joint public-state scenarios. Winner payments, all deposits and
        // event probabilities must refer to the SAME future declarations.
        // Factoring the independent masks gives the previous survival rate;
        // values cannot be factored across nonlinear point/rank thresholds.
        for(unsigned mask=0;mask<16;++mask) {
            double branch=1;
            for(int p=0;p<4;++p)branch*=mask&(1u<<p)?q.locked[p]:1-q.locked[p];
            if(branch<=0)continue;
            bool locked=mask&(1u<<actor);
            double tsumoRate=(locked?TSUMO_LOCKED:TSUMO_UNLOCKED)*(locked?1:q.silent[actor]);
            double ronRate=locked?RON_LOCKED:RON_UNLOCKED;
            double tsumoSurvival=exp(-tsumoRate),ronSurvival=1,totalHit=0;
            array<double,4> hit{};
            for(int winner=0;winner<4;++winner) if(winner!=self && winner!=actor) {
                double survive=exp(-ronRate*((mask&(1u<<winner))?1:q.silent[winner]));
                hit[winner]=1-survive;totalHit+=hit[winner];ronSurvival*=survive;
            }
            out.survival+=branch*tsumoSurvival*ronSurvival;
            out.tsumoSurvival+=branch*tsumoSurvival;
            double tsumoWin=1-tsumoSurvival,ronWin=tsumoSurvival*(1-ronSurvival);
            for(int b=0;b<2;++b) {
                double ron=0;
                if(totalHit>0) for(int winner=0;winner<4;++winner)
                    ron+=hit[winner]/totalHit*ronValue[mask][winner][actor][b];
                // Retain a pooled single-winner value when simultaneous ron
                // is possible. The event probability is for at least one ron.
                out.stopped[b]+=branch*(tsumoWin*tsumoValue[mask][actor][b]+ronWin*ron);
                out.tsumoStopped[b]+=branch*tsumoWin*tsumoValue[mask][actor][b];
            }
        }
        out.survival=clamp(out.survival,0.,1.);
        return out;
    }
    void configure(int who,int actor,int tiles,bool replacementDraw,const array<PublicHazardPlayer,4>& states) {
        self=who;rootActor=actor>=0 && actor<4?actor:who;remaining=tiles;replacement=replacementDraw;players=states;
        for(int t=0;t<MAX_TIME;++t) {
            auto projected=project(t);
            for(int p=0;p<4;++p) timeline[t][p]=atProjection(p,projected);
        }
        fullSurvival=1;
        for(int p=0;p<4;++p) if(p!=self) fullSurvival*=timeline[0][p].survival;
    }
    OrderedStep orderedStep(int actor,int elapsed,double targetSurvival=-1) const {
        const auto& cell=timeline[clamp(elapsed,0,MAX_TIME-1)];
        const auto& event=cell[actor];OrderedStep out;
        double power=1;
        if(targetSurvival>=0 && targetSurvival!=fullSurvival && fullSurvival>0 && fullSurvival<1)
            power=log(clamp(targetSurvival,1e-12,1.))/log(fullSurvival);
        out.survival=power==1?event.survival:pow(event.survival,power);
        out.scale=power==1?1:(event.survival<1?(1-out.survival)/(1-event.survival):0);
        out.tsumoSurvival=1-out.scale*(1-event.tsumoSurvival);
        for(int b=0;b<2;++b) {
            out.tsumoStopped[b]=out.scale*event.tsumoStopped[b];
            out.ronStopped[b]=out.scale*(event.stopped[b]-event.tsumoStopped[b]);
        }
        return out;
    }
    OtherWinInterval interval(int who,int afterActor,int draws,double targetSurvival=-1,int elapsed=0) const {
        if(who!=self)throw runtime_error("opponent hazard observer mismatch");
        OtherWinInterval out;if(afterActor<0 || afterActor>=4)afterActor=self;
        double power=1;
        if(targetSurvival>=0 && targetSurvival!=fullSurvival && fullSurvival<1)
            power=log(clamp(targetSurvival,1e-12,1.))/log(fullSurvival);
        for(int step=1;step<=draws;++step) {
            int actor=(afterActor+step)%4;if(actor==self)continue;
            const auto& cell=timeline[clamp(elapsed+step-1,0,MAX_TIME-1)][actor];
            double survive=power==1?cell.survival:pow(cell.survival,power);
            double scale=power==1?1:(cell.survival<1?(1-survive)/(1-cell.survival):0);
            for(int b=0;b<2;++b) out.stopped[b]+=out.survival*scale*cell.stopped[b];
            out.survival*=survive;
        }
        return out;
    }
    json explain() const {
        array<double,4> survival{};array<array<double,2>,4> values{};
        for(int p=0;p<4;++p) {
            survival[p]=timeline[0][p].survival;
            if(survival[p]<1) for(int b=0;b<2;++b) values[p][b]=timeline[0][p].stopped[b]/(1-survival[p]);
        }
        json projected=json::array();
        for(int time:{0,4,12,24}) if(time<=remaining)projected.push_back({{"elapsed_normal_clock",time},{"state",project(time).explain()}});
        return {{"mode","public_time_with_joint_declaration_values"},{"actor_survival",survival},{"actor_values",values},
            {"full_cycle_survival",fullSurvival},{"projections",projected},
            {"tsumo_unlocked",TSUMO_UNLOCKED},{"tsumo_locked",TSUMO_LOCKED},
            {"ron_unlocked",RON_UNLOCKED},{"ron_locked",RON_LOCKED},
            {"declaration_rate",DECLARATION_RATE},{"progress_rate",PROGRESS_RATE}};
    }
};
}
