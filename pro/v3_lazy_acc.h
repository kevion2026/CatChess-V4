#pragma once
// Search-only accumulator stack. Board rules, hash and repetition stay eager.
// Materialize the neural state only when a node actually needs an evaluation.
#include <cassert>
namespace v3lazy {
struct Frame {
    v2engine::Accumulator acc;
    int from,to,piece,victim;
    bool valid=false;
};
static Frame frames[MAXPLY+3];
static int level=0;
static bool active=false;
inline void begin(){
    assert(!active);level=0;frames[0].acc=flashAcc;frames[0].valid=true;active=true;
}
inline void push(int from,int to,int piece,int victim){
    assert(active && level<MAXPLY+1);
    auto& f=frames[++level];f.from=from;f.to=to;f.piece=piece;f.victim=victim;f.valid=false;
}
inline void pop(){assert(active && level>0);--level;}
inline const v2engine::Accumulator& current(){
    if(!active)return flashAcc;
    int base=level;while(!frames[base].valid)--base;
    while(base<level){
        auto& next=frames[base+1];next.acc=frames[base].acc;
        next.acc.template move<false>(next.from,next.to,next.piece,next.victim);
        next.valid=true;++base;
    }
#ifdef V3_LAZY_TEST
    v2engine::Accumulator expected;expected.reset(B);
    assert(std::memcmp(&expected,&frames[level].acc,sizeof expected)==0);
#endif
    return frames[level].acc;
}
inline void end(){assert(active && level==0);active=false;}
struct Scope {Scope(){begin();}~Scope(){end();}};
}
