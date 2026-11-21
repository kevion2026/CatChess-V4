// Pro's search uses the same board, trained geometry network and move rules.
// Flash keeps its original search. This file is included by xq.cpp.
#pragma once
#ifdef PRO2_SEARCH_TEST
#include <cassert>
#endif

static_assert(sizeof(TTEntry) == 16);
static uint8_t proGeneration = 0;
static int proStatic[MAXPLY + 2];
static int proPiece[MAXPLY + 2];
#ifdef V4_CAPTURE_HISTORY
static int16_t v4CaptureHistory[14][90][8];
static int v4CaptureScore(int move){return v4CaptureHistory[pidx(B[move/100])-1][move%100][abs(B[move%100])];}
static void v4CaptureUpdate(int move,int bonus){auto& h=v4CaptureHistory[pidx(B[move/100])-1][move%100][abs(B[move%100])];h+=bonus-int(h)*abs(bonus)/16384;}
#endif
#ifndef V3_CONTEMPT
#define V3_CONTEMPT 0
#endif
static int proRootSide;
static int proDrawValue(){return us==proRootSide?-V3_CONTEMPT:V3_CONTEMPT;}
#ifdef V3_CORRECTION
static int16_t v3Correction[2][16384];
static int correctedEval(int raw) {return raw+v3Correction[us==RED?0:1][STRUCTKEY&16383]/128;}
static void updateCorrection(int raw,int value,int depth) {
  int16_t& h=v3Correction[us==RED?0:1][STRUCTKEY&16383];
  const int target=clamp((value-raw)*128,-16384,16384),weight=min(32,depth*depth+1);
  h=clamp((int(h)*(256-weight)+target*weight)/256,-16384,16384);
}
#else
static int correctedEval(int raw) {return raw;}
#endif
static int16_t proContinuation[65536];
static unsigned flashContinuationIndex(int oldPiece,int oldTo,int piece,int to){
    uint32_t x=uint32_t(((oldPiece*90+oldTo)*14+piece)*90+to);
    x^=x>>16;x*=0x7feb352du;x^=x>>15;return x&65535;
}
static int proReduction[MAXPLY][128];
#if defined(CATCHESS_MAX21) && defined(PRO2_SEARCH_TEST)
static int max21SingularProbes=0;
#endif

static int proToTT(int value, int ply) {
  return value >= MATE - MAXPLY ? value + ply : value <= -MATE + MAXPLY ? value - ply : value;
}
static int proFromTT(int value, int ply) {
  return value >= MATE - MAXPLY ? value - ply : value <= -MATE + MAXPLY ? value + ply : value;
}
static TTEntry* proProbe(uint64_t key) {
  TTEntry* bucket = ttProbe(key);
  for (int i = 0; i < 4; ++i) if (bucket[i].key == key) return bucket + i;
  return nullptr;
}
static void proStore(uint64_t key, int depth, int value, int flag, int move, int ev, int ply) {
  TTEntry* slot = ttProbe(key);
  int worst = 100000;
  for (int i = 0; i < 4; ++i) {
    TTEntry* e = ttProbe(key) + i;
    if (e->key == key) {
      if (!move) move = e->mv;
      if (depth + 3 < e->depth && flag != 0) { e->flag = (e->flag & 3) | proGeneration; return; }
      slot = e;
      break;
    }
    const int age = uint8_t(proGeneration - (e->flag & 252)) / 4;
    const int quality = e->key ? e->depth - 8 * age : -10000;
    if (quality < worst) { worst = quality; slot = e; }
  }
  *slot = {key, int16_t(proToTT(value, ply)), int16_t(ev), uint8_t(depth),
           uint8_t(flag | proGeneration), uint16_t(move)};
}

static int proValue(int piece) { return abs(piece) == KING ? 20000 : PV[abs(piece)]; }

// Rebuild attackers after every exchange: moving a cannon screen, a horse leg
// or an elephant eye can create or remove an attack. Pinned recaptures are
// rejected against the actual temporary board, including facing generals.
static int proLeastAttacker(int target, int side) {
  int candidates[40], count = 0;
  const int x = fx(target), y = fy(target);
  auto add = [&](int a, int b, int type) {
    if (onBoard(a, b) && B[sqOf(a,b)] == side * type) candidates[count++] = sqOf(a,b);
  };
  add(x, y + side, PAWN);
  if (crossedRiver(y, side)) { add(x - 1, y, PAWN); add(x + 1, y, PAWN); }
  const int hx[] = {1,2,2,1,-1,-2,-2,-1}, hy[] = {2,1,-1,-2,-2,-1,1,2};
  for (int i = 0; i < 8; ++i) {
    const int a = x + hx[i], b = y + hy[i];
    if (!onBoard(a,b)) continue;
    const int legX = a - (abs(hx[i]) == 2 ? hx[i] / 2 : 0);
    const int legY = b - (abs(hy[i]) == 2 ? hy[i] / 2 : 0);
    if (!B[sqOf(legX,legY)]) add(a,b,HORSE);
  }
  for (int a : {-1,1}) for (int b : {-1,1}) {
    if (inPalace(x,y,side)) add(x+a,y+b,ADVISOR);
    if ((side == RED ? y >= 5 : y <= 4) && onBoard(x+2*a,y+2*b) && !B[sqOf(x+a,y+b)])
      add(x+2*a,y+2*b,ELEPHANT);
  }
  const int dx[] = {1,-1,0,0}, dy[] = {0,0,1,-1};
  for (int d = 0; d < 4; ++d) {
    if (inPalace(x,y,side)) add(x+dx[d],y+dy[d],KING);
    int a=x, b=y, blockers=0;
    while (onBoard(a+=dx[d], b+=dy[d])) {
      const int p=B[sqOf(a,b)];
      if (!p) continue;
      if (blockers == 0 && p == side * ROOK) candidates[count++] = sqOf(a,b);
      if (blockers == 1) { if (p == side * CANNON) candidates[count++] = sqOf(a,b); break; }
      ++blockers;
    }
  }
  while (count) {
    int best=0;
    for (int i=1; i<count; ++i) if (proValue(B[candidates[i]]) < proValue(B[candidates[best]]) ||
        (proValue(B[candidates[i]]) == proValue(B[candidates[best]]) && candidates[i]<candidates[best])) best=i;
    const int from=candidates[best], piece=B[from], victim=B[target];
    candidates[best]=candidates[--count];
    B[from]=0; B[target]=piece;
    const bool legal=!inCheck(side);
    B[from]=piece; B[target]=victim;
    if (legal) return from;
  }
  return -1;
}
static bool proSee(int move, int threshold) {
  const int from=move/100, target=move%100, piece=B[from], victim=B[target];
  if (proValue(victim) - proValue(piece) >= threshold) return true;
  if (proValue(victim) < threshold) return false;
  int8_t saved[90]; memcpy(saved,B,sizeof B);
  int gain[32], n=0;
  gain[0]=proValue(victim);
  B[from]=0; B[target]=piece;
  int side = piece > 0 ? BLACK : RED;
  while (n < 30 && abs(B[target]) != KING) {
    const int attacker=proLeastAttacker(target,side);
    if (attacker < 0) break;
    gain[n+1]=proValue(B[target])-gain[n];
    ++n;
    B[target]=B[attacker]; B[attacker]=0;
    side=-side;
  }
  while (n) { gain[n-1]=-max(-gain[n-1],gain[n]); --n; }
  memcpy(B,saved,sizeof B);
  return gain[0] >= threshold;
}

static int proHistory(int side, int move, int ply) {
  int result=quietHistory(side,move);
  const int piece=pidx(B[move/100])-1, to=move%100;
  for (int back : {1,2}) if (ply >= back && plyMove[ply-back])
    result += proContinuation[flashContinuationIndex(proPiece[ply-back],plyMove[ply-back]%100,piece,to)];
  return result;
}
static void proUpdateHistory(int side, int move, int ply, int bonus) {
  updateQuiet(side,move,bonus);
  const int piece=pidx(B[move/100])-1, to=move%100;
  for (int back : {1,2}) if (ply >= back && plyMove[ply-back]) {
    int16_t& entry=proContinuation[flashContinuationIndex(proPiece[ply-back],plyMove[ply-back]%100,piece,to)];
    entry += bonus - int(entry)*abs(bonus)/16384;
  }
}

struct ProMoves {
  int moves[128], scores[128], count;
  LegalProbe probe;
#ifdef V3_LAZY_SEE
  int deferred[128],deferredCount=0;
  bool ready=false;
#endif
#ifdef V4_LAZY_ORDER
  bool ordered=false;
  int savedSide,savedMove,savedPly;
#endif
  explicit ProMoves(int side, int ttMove, int ply) : probe(side) {
    count=genPseudo(side,moves);
#ifdef V4_LAZY_ORDER
    savedSide=side;savedMove=ttMove;savedPly=ply;
  }
  void orderMoves(){
    if(ordered)return;ordered=true;
    const int side=savedSide,ttMove=savedMove,ply=savedPly;
#endif
    for (int i=0; i<count; ++i) {
      const int m=moves[i], victim=B[m%100];
      if (m == ttMove) scores[i]=2000000;
      else if (victim) {
#ifdef V3_LAZY_SEE
        scores[i]=16*PV[abs(victim)]-PV[abs(B[m/100])];
        deferred[deferredCount++]=i;
#else
        scores[i]=(proSee(m,0)?1000000:-1000000)+16*PV[abs(victim)]-PV[abs(B[m/100])];
#endif
#ifdef V4_CAPTURE_HISTORY
        scores[i]+=v4CaptureScore(m)/8;
#endif
      }
      else if (m == killers[0][ply]) scores[i]=900000;
      else if (m == killers[1][ply]) scores[i]=800000;
      else if (ply && plyMove[ply-1] && m == counterTbl[plyMove[ply-1]]) scores[i]=700000;
      else scores[i]=proHistory(side,m,ply);
    }
  }
  bool hasLegal() const {
    for (int i=0; i<count; ++i) if (probe.legal(moves[i])) return true;
    return false;
  }
  int next(int index) {
#ifdef V4_LAZY_ORDER
    orderMoves();
#endif
#ifdef V3_LAZY_SEE
    // Preserve entry-time quiet histories. Capture exchanges depend only on
    // this board, so delay them until the node survives all early pruning.
    if(!ready){
      for(int j=0;j<deferredCount;++j){const int i=deferred[j];scores[i]+=proSee(moves[i],0)?1000000:-1000000;}
      ready=true;
    }
#endif
    int best=index;
    for (int i=index+1; i<count; ++i) if (scores[i]>scores[best] ||
        (scores[i]==scores[best] && moves[i]<moves[best])) best=i;
    swap(scores[index],scores[best]); swap(moves[index],moves[best]);
    return moves[index];
  }
};

static int qsearch(int alpha, int beta, int ply, int qdepth=0) {
#ifdef PRO2_SEARCH_TEST
  assert(alpha < beta);
#endif
  ++NODES;
  if (timeUp()) return 0;
  const int original=alpha;
  const uint64_t key=KEY;
#ifdef V4_EARLY_QTT
  // A cached bound can avoid generating the whole quiet move set. Preserve
  // repetition precedence; terminal positions without moves are never cached.
  const bool cycle=repeated();
  if(!cycle && beta-alpha==1){
    if(const TTEntry* cached=proProbe(key)){
      const int value=proFromTT(cached->score,ply),flag=cached->flag&3;
      if(flag==0 || (flag==1 && value>=beta) || (flag==2 && value<=alpha))return value;
    }
  }
#endif
  const LegalProbe legal(us);
  int moves[128], n=genPseudo(us,moves), first=0;
  while (first<n && !legal.legal(moves[first])) ++first;
  if (first==n) return -MATE+ply;
  if (repeated()) return proDrawValue();
  if (ply>=MAXPLY-1) return evalPos(us);
  TTEntry* entry=proProbe(key);
  TTEntry savedEntry;
  if(entry){savedEntry=*entry;entry=&savedEntry;}
  int ttMove=entry?entry->mv:0;
  if (entry && beta-alpha==1) {
    const int value=proFromTT(entry->score,ply), flag=entry->flag&3;
    if (flag==0 || (flag==1 && value>=beta) || (flag==2 && value<=alpha)) return value;
  }
  const int rawEv=legal.checked?32000:entry?entry->eval:evalPos(us);
  const int ev=legal.checked?32000:correctedEval(rawEv);
  int best=legal.checked?-MATE:ev, bestMove=0;
  if (!legal.checked) {
    if (best>=beta) { proStore(key,0,best,1,0,rawEv,ply); return best; }
    alpha=max(alpha,best);
  }
  pair<int,int> order[128]; int count=0;
  for (int i=first; i<n; ++i) {
    const int m=moves[i], victim=B[m%100];
    if (!legal.checked && !victim) {
#ifdef V3_QCHECKS
      if(qdepth || beta-alpha<=1)continue;
      const int from=m/100,to=m%100,piece=B[from];B[from]=0;B[to]=piece;
      const bool gives=inCheck(-us);B[from]=piece;B[to]=0;
      if(!gives)continue;
#else
      continue;
#endif
    }
    const int score=m==ttMove?2000000:victim?1000000+16*PV[abs(victim)]-PV[abs(B[m/100])]:proHistory(us,m,ply);
    order[count++]={score,m};
  }
  sort(order,order+count,[](const auto& a,const auto& b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
  for (int i=0; i<count; ++i) {
    const int move=order[i].second;
    if (move!=moves[first] && !legal.legal(move)) continue;
    const bool poor=!legal.checked && B[move%100] && !proSee(move,-70);
    if (!legal.checked && B[move%100] && ev+PV[abs(B[move%100])]+180<alpha && !poor) continue;
    plyMove[ply]=move; proPiece[ply]=pidx(B[move/100])-1;
    Undo undo; makeMove(move,undo);
    if (poor && !inCheck(us)) { unmakeMove(move,undo); continue; }
    const int value=-qsearch(-beta,-alpha,ply+1,qdepth+1);
    unmakeMove(move,undo);
    if (STOP) return 0;
    if (value>best) { best=value; bestMove=move; }
    alpha=max(alpha,value);
    if (alpha>=beta) break;
  }
  proStore(key,0,best,best>=beta?1:best<=original?2:0,bestMove,rawEv,ply);
  return best;
}

static int negamax(int depth, int alpha, int beta, int ply, bool allowNull, int excluded=0) {
#ifdef PRO2_SEARCH_TEST
  assert(alpha < beta);
#endif
  if (depth<=0) return qsearch(alpha,beta,ply);
  ++NODES;
  if (timeUp()) return 0;
  if (ply>=MAXPLY-1) return qsearch(alpha,beta,ply);
  if (repeated()) return proDrawValue();
  alpha=max(alpha,-MATE+ply); beta=min(beta,MATE-ply-1);
  if (alpha>=beta) return alpha;
  const bool pvNode=beta-alpha>1;
  const int original=alpha;
  const uint64_t key=KEY;
  TTEntry* entry=proProbe(key);
  TTEntry savedEntry;
  if(entry){savedEntry=*entry;entry=&savedEntry;}
  const int ttMove=entry?entry->mv:0;
  if (!excluded && entry && entry->depth>=depth && !pvNode) {
    const int value=proFromTT(entry->score,ply), flag=entry->flag&3;
    if (flag==0 || (flag==1 && value>=beta) || (flag==2 && value<=alpha)) return value;
  }
  ProMoves picker(us,ttMove,ply);
  if (!picker.hasLegal()) return -MATE+ply;
  const bool checked=picker.probe.checked;
  const int rawEv=checked?32000:entry?entry->eval:evalPos(us);
  const int ev=checked?32000:correctedEval(rawEv);
#ifdef V4_TT_EVAL
  int boundEv=ev;
  if(!checked && entry){
    const int value=proFromTT(entry->score,ply),flag=entry->flag&3;
    if(abs(value)<12000 && (flag==0 || (flag==1 && value>ev) || (flag==2 && value<ev)))boundEv=value;
  }
#else
  const int boundEv=ev;
#endif
  proStatic[ply]=ev;
  const bool improving=ply>=2 && ev!=32000 && proStatic[ply-2]!=32000 && ev>proStatic[ply-2];
  const bool prune=!excluded && !pvNode && !checked && abs(beta)<MATE-MAXPLY;
  if (prune) {
    if (depth<=6 && boundEv-(improving?65:85)*depth>=beta && abs(boundEv)<12000) return boundEv;
    if (depth<=2 && ev+180*depth<alpha) {
      const int value=qsearch(alpha,beta,ply);
      if (value<=alpha) return value;
    }
    if (allowNull && depth>=3 && ev>=beta && abs(ev)<1800 && matNonKing()>=NULL_MIN_MAT) {
      // Require an active piece on the moving side, not just enemy material.
      bool active=false;
      for (int i=0; i<90; ++i) if (B[i]*us>0 && abs(B[i])>=HORSE && abs(B[i])<=CANNON) {active=true;break;}
      if (active) {
        const int reduction=3+depth/4+min((ev-beta)/200,2);
        const int oldRep=repStart; const uint64_t oldKey=KEY;
        repStart=histLen+1; KEY^=ZSIDE; us=-us; plyMove[ply]=0;
        const int value=-negamax(depth-reduction,-beta,-beta+1,ply+1,false);
        us=-us; KEY=oldKey; repStart=oldRep;
        if (STOP) return 0;
        if (value>=beta && abs(value)<MATE-MAXPLY) return value;
      }
    }
  }
#ifdef V4_PROBCUT
  // Verify promising captures at a raised threshold before a reduced search.
  // The speculative result remains a bound, never an exact score.
  if(prune && depth>=5 && abs(beta)<10000){
    const int probBeta=min(12000,beta+170-40*improving);
    const bool contradicted=entry && entry->depth>=depth-3 && proFromTT(entry->score,ply)<probBeta && (entry->flag&3)!=1;
    if(!contradicted){
      int tried=0;
      for(int i=0;i<picker.count && tried<3;++i){
        const int move=picker.moves[i];
        if(!B[move%100] || !proSee(move,probBeta-ev) || !picker.probe.legal(move))continue;
        ++tried;plyMove[ply]=move;proPiece[ply]=pidx(B[move/100])-1;
        Undo undo;makeMove(move,undo);
        int value=-qsearch(-probBeta,-probBeta+1,ply+1);
        if(!STOP && value>=probBeta)value=-negamax(depth-4,-probBeta,-probBeta+1,ply+1,true);
        unmakeMove(move,undo);
        if(STOP)return 0;
        if(value>=probBeta){proStore(key,depth-3,value,1,move,rawEv,ply);return value;}
      }
    }
  }
#endif
  if (depth>=5 && !ttMove) --depth;
  int singularMove=0,singularExtension=1;
#if defined(CATCHESS_MAX21) && !defined(MAX21_NO_SINGULAR)
  // Spend extra depth only when a cached principal move has no close rival.
  // Copy TT fields before verification, since recursion can replace its slot.
  if(!excluded && depth>=7 && entry && ttMove && !checked && ply<MAXPLY-8 &&
     entry->depth>=depth-3 && (entry->flag&3)!=2 && std::abs(entry->score)<12000) {
#ifdef PRO2_SEARCH_TEST
    ++max21SingularProbes;
#endif
    const int singularBeta=proFromTT(entry->score,ply)-2*depth;
    const int alternative=negamax((depth-1)/2,singularBeta-1,singularBeta,ply,false,ttMove);
    proStatic[ply]=ev;
    if(STOP) return 0;
    if(alternative<singularBeta){
      singularMove=ttMove;
#ifdef V4_DOUBLE_SINGULAR
      singularExtension=(!pvNode && alternative<singularBeta-60 && ply<24)?2:1;
#endif
    }
  }
#endif
  int best=-MATE, bestMove=0, searched=0, quietCount=0, quiets[128];
  for (int i=0; i<picker.count; ++i) {
    const int move=picker.next(i);
    if(move==excluded) continue;
    if (!picker.probe.legal(move)) continue;
    const bool capture=B[move%100]!=0;
#ifdef V4_BAD_CAPTURE_LMR
    const bool badCapture=capture && depth>=4 && searched>=3 && !proSee(move,0);
#endif
    const int history=capture?0:proHistory(us,move,ply);
    plyMove[ply]=move; proPiece[ply]=pidx(B[move/100])-1;
    Undo undo; makeMove(move,undo);
    const bool gives=inCheck(us);
    if (searched && prune && !capture && !gives && best>-MATE+MAXPLY) {
      if ((depth<=5 && quietCount>=(improving?5:3)+depth*depth) ||
          (depth<=3 && ev+110*depth+history/90<=alpha && history<5000)) {
        unmakeMove(move,undo); continue;
      }
    }
    const int extension=move==singularMove?singularExtension:((gives && ply<2*depth+8 && ply<48)?1:0);
    const int nextDepth=depth-1+extension;
    int value;
    if (!searched) value=-negamax(nextDepth,-beta,-alpha,ply+1,true);
    else {
      int reduction=0;
      if (depth>=3 && searched>=2 && !capture && !gives && !checked) {
        reduction=proReduction[min(depth,MAXPLY-1)][min(searched,127)];
        reduction+=!pvNode; reduction+=!improving;
        reduction-=history/5000;
        reduction-=move==killers[0][ply];
        reduction=clamp(reduction,0,max(0,nextDepth-1));
      }
#ifdef V4_BAD_CAPTURE_LMR
      if(badCapture && !gives && !checked)
        reduction=clamp(1+(!pvNode)+(searched>=8),0,max(0,nextDepth-1));
#endif
      value=-negamax(nextDepth-reduction,-alpha-1,-alpha,ply+1,true);
      if (!STOP && reduction && value>alpha) value=-negamax(nextDepth,-alpha-1,-alpha,ply+1,true);
      if (!STOP && value>alpha && value<beta) value=-negamax(nextDepth,-beta,-alpha,ply+1,true);
    }
    unmakeMove(move,undo);
    if (STOP) return 0;
    ++searched;
    if (value>best) {best=value;bestMove=move;}
    alpha=max(alpha,value);
    if (alpha>=beta) {
#ifdef V4_CAPTURE_HISTORY
      if(capture)v4CaptureUpdate(move,min(1800,32*depth*depth));
#endif
      if (!capture) {
        killers[1][ply]=killers[0][ply];killers[0][ply]=move;
        const int bonus=min(1600,40*depth*depth);
        proUpdateHistory(us,move,ply,bonus);
        for (int q=0;q<quietCount;++q) proUpdateHistory(us,quiets[q],ply,-bonus/2);
        if (ply && plyMove[ply-1]) counterTbl[plyMove[ply-1]]=move;
      }
      break;
    }
    if (!capture) quiets[quietCount++]=move;
  }
  if(excluded) return searched?best:original;
#ifdef V3_CORRECTION
  // Only learn from quiet, non-mate results consistent with the search bound.
  if(!checked && abs(best)<12000 && (!bestMove || !B[bestMove%100]) &&
     !(best>=beta && best<ev) && !(best<=original && best>ev)) updateCorrection(rawEv,best,depth);
#endif
  proStore(key,depth,best,best>=beta?1:best<=original?2:0,bestMove,rawEv,ply);
  return best;
}

#ifdef V3_MATE_PROBE
#include "v3_mate.h"
#endif
static int rootBest=0;
static int searchRoot(int maxDepth,int ms) {
#ifdef V3_LAZY_ACC
  v3lazy::Scope lazyScope;
#endif
  STOP=false;NODES=0;proRootSide=us;
  proGeneration=uint8_t(proGeneration+4);
  fill_n(proStatic,MAXPLY+2,32000);
  for (int d=1;d<MAXPLY;++d) for (int m=1;m<128;++m)
#ifndef V4_LMR_DIVISOR
#define V4_LMR_DIVISOR 2.4
#endif
    proReduction[d][m]=int(0.25+log(double(d))*log(double(m))/V4_LMR_DIVISOR);
  for (auto& side:butterfly) for (auto& from:side) for (int& h:from) h=h*3/4;
  CLOCK_EVERY_NODE=ms>0 && ms<=30;
  DEADLINE=chrono::steady_clock::now()+chrono::milliseconds(ms>0?ms:86400000);
  int moves[128],n=genLegal(us,moves);
  if (!n) return 0;
  rootBest=moves[0]; int bestScore=0;
#ifdef V32_ROOT_POLICY
  // Order root moves by a one-ply value lookahead before the first search
  // iteration. The lazy accumulator makes make/unmake exact, so this costs one
  // static evaluation per root move (only at the root). Late moves receive the
  // largest LMR reductions, so a sane initial order measurably improves them.
  {
    int cscore[128];
    for (int i = 0; i < n; ++i) {
      const int mv = moves[i];
      Undo u; makeMove(mv, u);
      cscore[i] = -evalPos(us);   // makeMove flips us; negate back to our view
      unmakeMove(mv, u);
    }
    for (int i = 1; i < n; ++i)
      for (int j = i; j > 0; --j)
        if (cscore[j] > cscore[j-1] || (cscore[j] == cscore[j-1] && moves[j] < moves[j-1])) {
          const int cs = cscore[j]; cscore[j] = cscore[j-1]; cscore[j-1] = cs;
          const int mv = moves[j]; moves[j] = moves[j-1]; moves[j-1] = mv;
        } else break;
  }
#endif
#ifdef V3_MATE_PROBE
  if(ms>=100){
    const auto proof=v3mate::probe(min(50,ms/12));
    if(proof.move){
      cerr<<"info string independent checking mate proved within "<<proof.plies<<" plies, "<<proof.nodes<<" proof nodes"<<endl;
      cerr<<"info depth "<<proof.plies<<" score "<<MATE-proof.plies<<" nodes "<<NODES<<" time 0 nps 0"<<endl;
      return proof.move;
    }
  }
#endif
  const auto start=chrono::steady_clock::now();
  for (int depth=1;depth<=maxDepth;++depth) {
    int delta=30, alpha=depth>=5?max(-MATE,bestScore-delta):-MATE;
    int beta=depth>=5?min(MATE,bestScore+delta):MATE;
    int best=-MATE,bestMove=0;
    uint64_t effort[128]{};
    while (true) {
      best=-MATE;bestMove=0;
      int window=alpha;
      for (int i=0;i<n;++i) {
        const int move=moves[i];
        plyMove[0]=move;proPiece[0]=pidx(B[move/100])-1;
        Undo undo;makeMove(move,undo);
        const bool checking=inCheck(us);
        const int extension=checking?1:0;
        const uint64_t before=NODES;
        int value;
        if (!i) value=-negamax(depth-1+extension,-beta,-window,1,true);
        else {
#ifdef V3_ROOT_LMR
#ifdef V3_DEEP_ROOT
          const int reduction=depth>=4 && i>=3 && !undo.cap && !checking?
            clamp(proReduction[min(depth,MAXPLY-1)][min(i,127)]+(i>=8)-(depth<=5),0,depth-2):0;
#else
          const int reduction=depth>=4 && i>=3 && !undo.cap && !checking?1+(depth>=7 && i>=12):0;
#endif
          value=-negamax(depth-1+extension-reduction,-window-1,-window,1,true);
          if(!STOP && reduction && value>window)value=-negamax(depth-1+extension,-window-1,-window,1,true);
#else
          value=-negamax(depth-1+extension,-window-1,-window,1,true);
#endif
          if (!STOP && value>window && value<beta) value=-negamax(depth-1+extension,-beta,-window,1,true);
        }
        unmakeMove(move,undo);
        effort[i]+=NODES-before;
        if (STOP) break;
        if (value>best) {best=value;bestMove=move;}
#ifdef V3_PARTIAL_ROOT
        if(value>window)rootBest=move;
#endif
        window=max(window,value);
        // A fail-high is a lower bound. Widen before searching other moves;
        // continuing here would pass an inverted alpha-beta window.
        if (window>=beta) break;
      }
      if (STOP) break;
      const int position=int(find(moves,moves+n,bestMove)-moves);
      if (position<n) {swap(moves[0],moves[position]);swap(effort[0],effort[position]);}
      if (best<=alpha && alpha>-MATE) alpha=max(-MATE,best-delta);
      else if (best>=beta && beta<MATE) beta=min(MATE,best+delta);
      else break;
      delta=min(MATE,delta*2);
    }
    if (STOP) break;
    rootBest=bestMove;bestScore=best;
    // Keep the PV first; expensive alternatives are examined early next time.
    for (int i=1;i<n;++i) for (int j=i+1;j<n;++j) if (effort[j]>effort[i]) {
      swap(moves[i],moves[j]);swap(effort[i],effort[j]);
    }
    const auto elapsed=chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now()-start).count();
    cerr<<"info depth "<<depth<<" score "<<best<<" nodes "<<NODES<<" time "<<elapsed
        <<" nps "<<(elapsed?NODES*1000/elapsed:0)<<endl;
    if (abs(best)>MATE-MAXPLY) break;
  }
  return rootBest;
}
