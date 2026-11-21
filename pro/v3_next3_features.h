#pragma once
#define V2_PHASE_HEAD
#include "flash2_features.h"
#include "v3_exact_lines.h"
#ifdef V4_FAST_GEOMETRY
#include "v4pro_lines.h"
#endif
namespace v3next3 {
// Residual king buckets extend our own trained Max encoder. Zero residuals
// preserve the parent exactly; data can then specialize each palace square.
constexpr int SharedInputs=1260,AbsoluteInputs=SharedInputs*10,RelationInputs=1008;
constexpr int BoardInputs=AbsoluteInputs+RelationInputs,AbsoluteWidth=64,RelationWidth=32,AccWidth=96;
constexpr int BoardWidth=AccWidth*2,ProductWidth=32,GeoWidth=160,HiddenWidth=64;
#ifdef V32_GEORES
constexpr int GeoResWidth=32; // direct geometry -> value residual pathway
#endif
constexpr int HeadWidth=BoardWidth+ProductWidth+GeoWidth,TokensPerPiece=6,ViewFlag=16384;
constexpr int EmbeddingParams=AbsoluteInputs*AbsoluteWidth+RelationInputs*RelationWidth;
constexpr float BoardScale=2048.f,ValueScale=400.f;
#ifdef V4_FAST_GEOMETRY
using v4lines::lineGeometry;
#else
using v3lines::lineGeometry;
#endif
inline int start(int id){return(id>=ViewFlag?AccWidth:0)+(id%ViewFlag<AbsoluteInputs?0:AbsoluteWidth);}
inline int width(int id){return id%ViewFlag<AbsoluteInputs?AbsoluteWidth:RelationWidth;}
inline int offset(int id){id%=ViewFlag;return id<AbsoluteInputs?id*AbsoluteWidth:AbsoluteInputs*AbsoluteWidth+(id-AbsoluteInputs)*RelationWidth;}
inline void tokens(int piece,int sq,int side,const int* kings,int* ids){
 ids[0]=flash2::feature(piece,sq,side);
 int own=kings[side==1?0:1];if(own<0)own=side==1?85:4;own=side==1?own:89-own;
 const int bucket=std::clamp(own/9-7,0,2)*3+std::clamp(own%9-3,0,2);
 ids[1]=SharedInputs+bucket*SharedInputs+ids[0];
 const int role=ids[0]/90,ps=side==1?sq:89-sq;
 for(int k=0;k<2;++k){
  int king=kings[(side==1?0:1)^k];if(king<0)king=((side==1?0:1)^k)==0?85:4;king=side==1?king:89-king;
  const int base=AbsoluteInputs+(role*2+k)*36;
  ids[2+2*k]=base+ps%9-king%9+8;ids[3+2*k]=base+17+ps/9-king/9+9;
 }
}
inline void addGeo(int32_t* raw,int id,int sign){for(int j=0;j<32;++j){const int d=sign*flash2::geoRows[id][j];raw[j]+=d;raw[(id<630?32:64)+j]+=d;}}
inline void geometry(const int32_t* raw,float* out){for(int k=0;k<3;++k)flash2::geometry(raw+k*32,out+k*32);}
inline float prior(const int32_t* raw){return flash2::prior(raw);}
inline float phase(const int32_t* raw){return flash2::phase(raw);}
inline void products(float* a){for(int i=0;i<ProductWidth;++i)a[BoardWidth+i]=a[i]*a[AbsoluteWidth+i]/3.f;}
}
