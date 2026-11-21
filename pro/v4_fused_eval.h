#pragma once
#include "v3_next3_features.h"
#ifndef V3_WEIGHTS_HEADER
#ifdef V3_JOINT
#define V3_WEIGHTS_HEADER "v3_joint_weights.h"
#else
#define V3_WEIGHTS_HEADER "v3_next3_weights.h"
#endif
#endif
#include V3_WEIGHTS_HEADER
#include <type_traits>
#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace v3next3 {
static_assert(std::extent_v<decltype(v3next3weights::EmbedBoard),1> == AbsoluteWidth);
static_assert(std::extent_v<decltype(v3next3weights::EmbedRelation),1> == RelationWidth);
static_assert(std::extent_v<decltype(v3next3weights::Dense),1> == HeadWidth);
inline const int16_t* coefficients(int id) {
    return id<AbsoluteInputs?v3next3weights::EmbedBoard[id]:v3next3weights::EmbedRelation[id-AbsoluteInputs];
}

// Compile additive feature banks into small fixed-king lookup tables once.
// These are derived caches, not new learned parameters.
struct CompiledBanks {
    alignas(32) int16_t absolute[9][1260][64];
    alignas(32) int16_t axial[2][9][1260][32];
    CompiledBanks(){
        for(int k=0;k<9;++k)for(int id=0;id<1260;++id){
            for(int j=0;j<64;++j){
                int x=int(v3next3weights::EmbedBoard[id][j])+v3next3weights::EmbedBoard[1260+k*1260+id][j];
                if(x < -32767 || x>32767)std::abort();absolute[k][id][j]=x;
            }
            const int role=id/90,sq=id%90;
            for(int owner=0;owner<2;++owner){
                const int king=(owner==0?7:0)*9+(k/3)*9+3+k%3;
                const int base=(role*2+owner)*36;
                for(int j=0;j<32;++j){
                    int x=int(v3next3weights::EmbedRelation[base+sq%9-king%9+8][j])+v3next3weights::EmbedRelation[base+17+sq/9-king/9+9][j];
                    if(x < -32767 || x>32767)std::abort();axial[owner][k][id][j]=x;
                }
            }
        }
    }
};
inline const CompiledBanks compiledBanks;
struct Accumulator {
    alignas(32) int32_t board[2][AccWidth]{};
    alignas(32) int32_t geo[2][96]{};
    int8_t squares[96]{};
    int kings[8]{};

    void token(int piece,int sq,int sign) {
        for(int view=0;view<2;++view) {
            const int side=view==0?1:-1,id=flash2::feature(piece,sq,side);
            int own=kings[view],enemy=kings[view^1];
            if(own<0)own=view==0?85:4;if(enemy<0)enemy=view==0?4:85;
            if(side==-1){own=89-own;enemy=89-enemy;}
            if(own/9>=7 && own%9>=3 && own%9<=5 && enemy/9<=2 && enemy%9>=3 && enemy%9<=5){
                const int a=(own/9-7)*3+own%9-3,b=(enemy/9)*3+enemy%9-3;
                const int16_t* absolute=compiledBanks.absolute[a][id];
                const int16_t* first=compiledBanks.axial[0][a][id];
                const int16_t* second=compiledBanks.axial[1][b][id];
                for(int j=0;j<64;++j)board[view][j]+=sign*int(absolute[j]);
                for(int j=0;j<32;++j)board[view][64+j]+=sign*(int(first[j])+second[j]);
            }else{
                int ids[TokensPerPiece];tokens(piece,sq,side,kings,ids);
                for(int f:ids){const int16_t* coeff=coefficients(f);for(int j=0;j<width(f);++j)board[view][start(f)+j]+=sign*coeff[j];}
            }
            addGeo(geo[view],id,sign);
        }
    }
    void rebuild() {
        std::fill_n(kings,8,0); kings[0]=kings[1]=-1;
        for(int s=0;s<90;++s) if(std::abs(squares[s])==1) kings[squares[s]>0?0:1]=s;
        for(int v=0;v<2;++v) {
            std::copy_n(v3next3weights::Bias,AccWidth,board[v]);
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
        result[i][j]=v3next3weights::Dense[j][i];
    return result;
}();
#ifdef V3_INT_DENSE
constexpr float InputScale=1024.f,DenseScale=8192.f;
alignas(32) inline const auto DenseInt=[]{
    std::array<std::array<int16_t,HeadWidth>,HiddenWidth> result{};
    for(int j=0;j<HiddenWidth;++j)for(int i=0;i<HeadWidth;++i)
        result[j][i]=std::lround(std::clamp(v3next3weights::Dense[j][i]*DenseScale,-32767.f,32767.f));
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
    using namespace v3next3weights;
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

namespace v3pro=v3next3;
namespace v3weights=v3next3weights;
