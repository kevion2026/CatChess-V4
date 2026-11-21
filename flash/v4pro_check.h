#pragma once
// A general in its own palace cannot be attacked by the opposing advisors or
// elephants. Check the remaining legal attack patterns using reverse topology.
namespace v4check {
struct HorseSource {uint8_t square,leg;};
struct HorseList {uint8_t count=0;HorseSource sources[8]{};};
struct ReverseHorses {
    HorseList targets[90]{};
    ReverseHorses(){
        constexpr int dx[8]={1,2,2,1,-1,-2,-2,-1},dy[8]={2,1,-1,-2,-2,-1,1,2};
        for(int s=0;s<90;++s)for(int d=0;d<8;++d){
            int x=s%9+dx[d],y=s/9+dy[d];if(unsigned(x)>=9 || unsigned(y)>=10)continue;
            int leg=(s/9+(std::abs(dy[d])==2?dy[d]/2:0))*9+s%9+(std::abs(dx[d])==2?dx[d]/2:0);
            auto& l=targets[y*9+x];l.sources[l.count++]={uint8_t(s),uint8_t(leg)};
        }
    }
};
inline const ReverseHorses horses;
inline bool checked(const int8_t* b,int k,int side){
    const int by=-side,x=k%9,y=k/9;
    if(side==1){if(y>0 && b[k-9]==-7)return true;}
    else if(y<9 && b[k+9]==7)return true;
    // At the defending palace every enemy pawn is on its crossed-river half.
    if((x>0 && b[k-1]==by*7)||(x<8 && b[k+1]==by*7))return true;
    const auto& h=horses.targets[k];
    for(int i=0;i<h.count;++i)if(b[h.sources[i].square]==by*4 && !b[h.sources[i].leg])return true;
    for(int d=0;d<4;++d){
        const auto& ray=v4lines::topology.rays[k][d];bool first=true;
        for(int i=0;i<ray.count;++i){
            const int q=ray.squares[i],p=b[q];if(!p)continue;
            if(first){
                if(p==by*5)return true;
                if(d>=2 && p==by && (by==1?q/9>=7:q/9<=2) && q%9>=3 && q%9<=5)return true;
                first=false;
            }else{if(p==by*6)return true;break;}
        }
    }
    return false;
}
}
