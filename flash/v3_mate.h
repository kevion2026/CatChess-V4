#pragma once
// Independent checking-mate companion. An attacker must keep checking; every
// legal defence must be proved losing. Only negative results are cached, so
// repetition-dependent cache entries can miss a mate but cannot invent one.
namespace v3mate {
enum Result { No=0,Yes=1,Unknown=2 };
struct Failure {uint64_t key=0;int depth=-1;};
static Failure failures[16384];
static int attacker,visited,maxNodes;
static bool exhausted;
static chrono::steady_clock::time_point until;
static bool limit(){
    if(exhausted)return true;
    if(visited>=maxNodes || (NODE_LIMIT && NODES>=NODE_LIMIT) ||
       ((visited&31)==0 && chrono::steady_clock::now()>=until))exhausted=true;
    return exhausted;
}
static Result solve(int remaining){
    if(limit())return Unknown;
    ++visited;++NODES;
    int moves[128],n=genLegal(us,moves);
    if(!n)return us==attacker?No:Yes;
    if(!remaining || repeated())return No;
    auto& cache=failures[KEY&16383];
    if(cache.key==KEY && cache.depth>=remaining)return No;
    const uint64_t key=KEY;
    const bool own=us==attacker;
    // Captures first, followed by deterministic move order. No neural or
    // external-engine result is involved in a proof.
    sort(moves,moves+n,[](int a,int b){
        const int x=PV[abs(B[a%100])],y=PV[abs(B[b%100])];
        return x!=y?x>y:a<b;
    });
    for(int i=0;i<n;++i){
        Undo u;makeMove(moves[i],u);
        Result result=No;
        const bool admissible=!own||inCheck(us);
        if(admissible)result=solve(remaining-1);
        unmakeMove(moves[i],u);
        if(result==Unknown)return Unknown;
        if(own && result==Yes)return Yes;
        if(!own && result==No){failures[key&16383]={key,remaining};return No;}
    }
    if(!own)return Yes;
    failures[key&16383]={key,remaining};return No;
}
struct Proof {int move=0,plies=0,nodes=0;};
static Proof probe(int milliseconds,int nodeBudget=6000,int maxDepth=21){
    attacker=us;visited=0;maxNodes=nodeBudget;exhausted=false;
    until=chrono::steady_clock::now()+chrono::milliseconds(milliseconds);
    for(auto& f:failures)f={};
    int moves[128],n=genLegal(us,moves),checks[128],count=0;
    for(int i=0;i<n;++i){Undo u;makeMove(moves[i],u);const bool check=inCheck(us);unmakeMove(moves[i],u);if(check)checks[count++]=moves[i];}
    if(!count)return {};
    for(int depth=1;depth<=maxDepth;depth+=4){
        for(int i=0;i<count;++i){
            Undo u;makeMove(checks[i],u);Result r=solve(depth-1);unmakeMove(checks[i],u);
            if(r==Yes)return {checks[i],depth,visited};
            if(r==Unknown)return {0,0,visited};
        }
    }
    return {0,0,visited};
}
}
