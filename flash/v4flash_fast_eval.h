#pragma once
#include "v4flash_features.h"
#ifndef V4FLASH_WEIGHTS_HEADER
#define V4FLASH_WEIGHTS_HEADER "v4flash_weights.h"
#endif
#include V4FLASH_WEIGHTS_HEADER
#include <type_traits>
#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace v4flash {
static_assert(std::extent_v<decltype(v4flashweights::EmbedBoard),1> == AbsoluteWidth);
static_assert(std::extent_v<decltype(v4flashweights::EmbedRelation),1> == RelationWidth);
static_assert(std::extent_v<decltype(v4flashweights::Dense),1> == HeadWidth);
inline const int16_t* coefficients(int id) {
    return id<AbsoluteInputs?v4flashweights::EmbedBoard[id]:v4flashweights::EmbedRelation[id-AbsoluteInputs];
}
#ifdef V4FLASH_FUSED_ROWS
struct FusedRows {
 alignas(32) int16_t absolute[9][1260][AbsoluteWidth];
 FusedRows(){for(int k=0;k<9;++k)for(int id=0;id<1260;++id)for(int j=0;j<AbsoluteWidth;++j){
  int value=int(v4flashweights::EmbedBoard[id][j])+v4flashweights::EmbedBoard[1260+(k/3)*1260+id][j];
#ifdef V4FLASH_SEPARABLE
  value+=v4flashweights::EmbedBoard[4*1260+(k%3)*1260+id][j];
#endif
  if(value<-32767||value>32767)std::abort();absolute[k][id][j]=value;
 }}
};
inline const FusedRows fusedRows;
#endif
struct Accumulator {
    alignas(32) int32_t board[2][AccWidth]{};
    alignas(32) int32_t geo[2][96]{};
    int8_t squares[96]{};
    int kings[8]{};

    void token(int piece,int sq,int sign) {
        for(int view=0;view<2;++view) {
            int ids[TokensPerPiece]; tokens(piece,sq,view==0?1:-1,kings,ids);
#ifdef V4FLASH_FUSED_ROWS
            int own=kings[view];if(own<0)own=view==0?85:4;if(view)own=89-own;
            const int bucket=std::clamp(own/9-7,0,2)*3+std::clamp(own%9-3,0,2);
            const int16_t* absolute=fusedRows.absolute[bucket][ids[0]];
            for(int j=0;j<AbsoluteWidth;++j)board[view][j]+=sign*int(absolute[j]);
            for(int k=TokensPerPiece-4;k<TokensPerPiece;++k){
                const int16_t* coeff=coefficients(ids[k]);
                for(int j=0;j<RelationWidth;++j)board[view][AbsoluteWidth+j]+=sign*int(coeff[j]);
            }
#else
            for(int id:ids) {
                int begin=start(id), n=width(id);
                const int16_t* coeff=coefficients(id);
                for(int j=0;j<n;++j) board[view][begin+j]+=sign*coeff[j];
            }
#endif
            addGeo(geo[view],ids[0],sign);
        }
    }
    void rebuild() {
        std::fill_n(kings,8,0); kings[0]=kings[1]=-1;
        for(int s=0;s<90;++s) if(std::abs(squares[s])==1) kings[squares[s]>0?0:1]=s;
        for(int v=0;v<2;++v) {
            std::copy_n(v4flashweights::Bias,AccWidth,board[v]);
            std::fill_n(geo[v],96,0);
        }
        for(int s=0;s<90;++s) if(squares[s]) token(squares[s],s,1);
    }
    void reset(const int8_t* src) {
        std::copy_n(src,90,squares); std::fill(squares+90,squares+96,0);
        rebuild();
    }
    template<bool Undo> void move(int from,int to,int piece,int victim) {
        if(std::abs(piece)==1 || std::abs(victim)==1) {
            squares[from]=Undo?piece:0;
            squares[to]=Undo?victim:piece;
            rebuild();
            return;
        }
        token(piece,Undo?to:from,-1);
        if(victim) token(victim,to,Undo?1:-1);
        token(piece,Undo?from:to,1);
        squares[from]=Undo?piece:0;
        squares[to]=Undo?victim:piece;
    }
};
alignas(32) inline constexpr auto DenseByInput=[] {
    std::array<std::array<float,HiddenWidth>,HeadWidth> result{};
    for(int i=0;i<HeadWidth;++i) for(int j=0;j<HiddenWidth;++j)
        result[i][j]=v4flashweights::Dense[j][i];
    return result;
}();
#ifdef V3_INT_DENSE
constexpr float InputScale=1024.f,DenseScale=8192.f;
alignas(32) inline const auto DenseInt=[]{
    std::array<std::array<int16_t,HeadWidth>,HiddenWidth> result{};
    for(int j=0;j<HiddenWidth;++j)for(int i=0;i<HeadWidth;++i)
        result[j][i]=std::lround(std::clamp(v4flashweights::Dense[j][i]*DenseScale,-32767.f,32767.f));
    return result;
}();
inline const bool DenseBound=[]{
    for(const auto& row:DenseInt){int64_t bound=0;for(int x:row)bound+=int64_t(std::abs(x))*int(InputScale*6);
        if(bound>2147483647)std::abort();}
    return true;
}();
#endif
#ifdef V4_SPARSE_DENSE
alignas(32) inline const auto PairDense=[] {
    std::array<std::array<int16_t,HiddenWidth*2>,HeadWidth/2> result{};
    for(int p=0;p<HeadWidth/2;++p)for(int j=0;j<HiddenWidth;++j){
        result[p][2*j]=DenseInt[j][2*p];result[p][2*j+1]=DenseInt[j][2*p+1];
    }
    return result;
}();
#endif
// Keep a single floating-point evaluation path: specialized/inlined copies
// can contract operations differently at a half-centipawn rounding boundary.
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline,noclone))
#elif defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
inline int score(const Accumulator& acc,int side) {
    using namespace v4flashweights;
    int view=side==1?0:1;
    alignas(32) float in[HeadWidth];
    for(int v=0;v<2;++v) for(int i=0;i<AccWidth;++i) in[v*AccWidth+i]=std::clamp(acc.board[view^v][i]/BoardScale,0.f,3.f);
    products(in);
    geometry(acc.geo[view],in+BoardWidth+ProductWidth);
#ifdef V3_LINES
    lineGeometry(acc.squares,side,in+BoardWidth+ProductWidth+96);
#endif
    for(int i=0;i<GeoWidth;++i) {
        float& x=in[BoardWidth+ProductWidth+i];
#ifdef V3_MUTE_GEOMETRY
        x=0;
#else
        x=std::clamp((x-Mean[i])*InvDev[i],-6.f,6.f);
#endif
    }
    const float mix=phase(acc.geo[view]);
    float result=prior(acc.geo[view])+Offset+mix*PhaseBias;
    alignas(32) float activation[HiddenWidth];
#if defined(V3_INT_DENSE) && defined(__AVX2__)
    alignas(32) int16_t quantized[HeadWidth];
    for(int i=0;i<HeadWidth;i+=16){
        __m256 scale=_mm256_set1_ps(InputScale);
        __m256i lo=_mm256_cvtps_epi32(_mm256_mul_ps(_mm256_load_ps(in+i),scale));
        __m256i hi=_mm256_cvtps_epi32(_mm256_mul_ps(_mm256_load_ps(in+i+8),scale));
        __m256i packed=_mm256_permute4x64_epi64(_mm256_packs_epi32(lo,hi),0xd8);
        _mm256_store_si256(reinterpret_cast<__m256i*>(quantized+i),packed);
    }
#ifdef V4_SPARSE_DENSE
    __m256i sums[HiddenWidth/8];
    for(auto& x:sums)x=_mm256_setzero_si256();
    for(int p=0;p<HeadWidth/2;++p){
        const uint32_t packed=uint16_t(quantized[2*p]) | (uint32_t(uint16_t(quantized[2*p+1]))<<16);
        if(!packed)continue;
        const __m256i input=_mm256_set1_epi32(int(packed));
#pragma GCC unroll 8
        for(int j=0;j<HiddenWidth/8;++j)
            sums[j]=_mm256_add_epi32(sums[j],_mm256_madd_epi16(input,_mm256_load_si256(reinterpret_cast<const __m256i*>(PairDense[p].data()+j*16))));
    }
    for(int j=0;j<HiddenWidth/8;++j){
        __m256 h=_mm256_add_ps(_mm256_loadu_ps(Hidden+j*8),_mm256_div_ps(_mm256_cvtepi32_ps(sums[j]),_mm256_set1_ps(InputScale*DenseScale)));
        _mm256_store_ps(activation+j*8,h);
    }
#else
    for(int j=0;j<HiddenWidth;++j){
        __m256i sum=_mm256_setzero_si256();
        for(int i=0;i<HeadWidth;i+=16)sum=_mm256_add_epi32(sum,_mm256_madd_epi16(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(quantized+i)),
            _mm256_load_si256(reinterpret_cast<const __m256i*>(DenseInt[j].data()+i))));
        __m128i s=_mm_add_epi32(_mm256_castsi256_si128(sum),_mm256_extracti128_si256(sum,1));
        s=_mm_hadd_epi32(s,s);s=_mm_hadd_epi32(s,s);
        activation[j]=Hidden[j]+_mm_cvtsi128_si32(s)/(InputScale*DenseScale);
    }
#endif
#elif defined(__AVX2__)
    __m256 hidden[HiddenWidth/8];
    for(int j=0;j<HiddenWidth/8;++j) hidden[j]=_mm256_loadu_ps(Hidden+j*8);
    for(int i=0;i<HeadWidth;++i) {
        __m256 value=_mm256_set1_ps(in[i]);
        for(int j=0;j<HiddenWidth/8;++j)
            hidden[j]=_mm256_add_ps(hidden[j],_mm256_mul_ps(value,_mm256_load_ps(DenseByInput[i].data()+j*8)));
    }
    for(int j=0;j<HiddenWidth/8;++j) _mm256_store_ps(activation+j*8,hidden[j]);
#else
    std::copy_n(Hidden,HiddenWidth,activation);
    for(int i=0;i<HeadWidth;++i) for(int j=0;j<HiddenWidth;++j)
        activation[j]+=in[i]*DenseByInput[i][j];
#endif
    for(int j=0;j<HiddenWidth;++j) result+=std::max(0.f,activation[j])*(Output[j]+mix*PhaseOutput[j]);
#ifdef V32_GEORES
    // Direct geometry -> value residual pathway. Geometry stays a first-class
    // input but now also owns a dedicated output channel, independently of the
    // fused main trunk.
    for(int j=0;j<GeoResWidth;++j){
        float s=GeoResBias[j];
        const float* wg=GeoRes[j];
        for(int i=0;i<GeoWidth;++i) s+=wg[i]*in[BoardWidth+ProductWidth+i];
        result+=std::max(0.f,s)*GeoResOut[j];
    }
#endif
    return std::lround(std::clamp(result*ValueScale,-12000.f,12000.f));
}
}

namespace v3pro=v4flash;
namespace v3weights=v4flashweights;
