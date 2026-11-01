#pragma once
// Output-only observations of existing v64 estimates and already selected overrides.
// No evaluator, probabilities model, action eligibility or score selection lives here.
namespace winout {
inline json value(double probability,bool available){return available?json(probability):json(nullptr);}
inline json afterRisk(double probability,bool available,double survival){return value(survival*probability,available);}
struct CallObservation {
    double probability=0;bool available=false;
    void evaluated(double p){probability=p;available=true;}
    void folded(){probability=0;available=true;}
    void nagashi(){available=false;}
    json output()const{return value(probability,available);}
};
inline json discardRow(const std::string& tile,double score,double off,bool riichi,
                       double survival,double loss,double probability,bool available,bool selected){
    return {{"tile",tile},{"score",score},{"off",off},{"riichi",riichi},
        {"current_p_ron",1-survival},{"current_loss",loss},
        {"win",value(probability,available)},{"win_after_current_risk",afterRisk(probability,available,survival)},
        {"win_status",available?"estimated_policy_current_hand_conditional_on_current_discard_passing":"nagashi_policy_win_not_computed"},
        {"selected",selected}};
}
inline json budgetFallback(const std::string& response,const json& estimates,double seconds,double budget){
    return {{"budget_fallback",true},{"response",response},{"estimates",estimates},
        {"estimate_status",estimates.is_null()?"not_computed":"last_complete_pass"},
        {"seconds",seconds},{"search_budget_seconds",budget}};
}
}
