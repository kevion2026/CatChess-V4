#pragma once
// Exact structural features, evaluated through precomputed board topology.
// No chess evaluation constants are added; all 64 outputs feed the learned head.
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
namespace v4lines {
struct Dest {uint8_t square,blocker;};
struct List {uint8_t count=0;Dest dest[8]{};};
struct Ray {uint8_t count=0,squares[9]{};};
struct Topology {
    Ray rays[90][4]{};
    List jumps[2][8][90]{};
    Topology(){
        constexpr int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
        auto inside=[](int x,int y){return unsigned(x)<9 && unsigned(y)<10;};
        auto palace=[](int x,int y,int side){return x>=3 && x<=5 && (side==1?y>=7:y<=2);};
        for(int s=0;s<90;++s){
            int x=s%9,y=s/9;
            for(int d=0;d<4;++d){
                auto& ray=rays[s][d];
                for(int a=x+dx[d],b=y+dy[d];inside(a,b);a+=dx[d],b+=dy[d])ray.squares[ray.count++]=b*9+a;
            }
            for(int v=0;v<2;++v)for(int t:{1,2,3,4,7}){
                auto& list=jumps[v][t][s];int side=v==0?1:-1;
                auto add=[&](int a,int b,int blocker=90){if(inside(a,b))list.dest[list.count++]={uint8_t(b*9+a),uint8_t(blocker)};};
                if(t==1){for(int d=0;d<4;++d)if(palace(x+dx[d],y+dy[d],side))add(x+dx[d],y+dy[d]);}
                else if(t==2 || t==3){
                    for(int a:{-1,1})for(int b:{-1,1}){
                        int xx=x+a*(t==3?2:1),yy=y+b*(t==3?2:1);
                        if(!inside(xx,yy))continue;
                        if(t==2){if(palace(xx,yy,side))add(xx,yy);}
                        else if(side==1?yy>=5:yy<=4)add(xx,yy,(y+b)*9+x+a);
                    }
                }else if(t==4){
                    constexpr int hx[8]={1,2,2,1,-1,-2,-2,-1},hy[8]={2,1,-1,-2,-2,-1,1,2};
                    for(int k=0;k<8;++k)add(x+hx[k],y+hy[k],(y+(std::abs(hy[k])==2?hy[k]/2:0))*9+x+(std::abs(hx[k])==2?hx[k]/2:0));
                }else{
                    add(x,y-side);
                    if(side==1?y<=4:y>=5){add(x-1,y);add(x+1,y);}
                }
            }
        }
    }
};
inline const Topology topology;
inline void lineGeometry(const int8_t* b,int side,float* out){
    uint8_t attack[2][96]{};uint16_t cheapest[2][96];
    std::fill_n(cheapest[0],96,30000);std::fill_n(cheapest[1],96,30000);
    int stats[64]{};int squares[90],count=0;
    constexpr int value[8]={0,20000,140,140,390,800,390,90};
    for(int s=0;s<90;++s)if(b[s]){
        squares[count++]=s;
        const int piece=b[s],own=piece>0?1:-1,v=piece*side>0?0:1,t=std::abs(piece),base=v*28+(t-1)*4;
        auto hit=[&](int q,bool mobility=true){
            ++attack[v][q];cheapest[v][q]=std::min<int>(cheapest[v][q],value[t]);
            stats[base]+=mobility && b[q]*own<=0;
        };
        if(t==5 || t==6){
            for(const auto& ray:topology.rays[s]){
                bool screen=false;
                for(int i=0;i<ray.count;++i){
                    const int q=ray.squares[i],p=b[q];
                    if(t==5){hit(q);if(p)break;}
                    else if(!screen){if(p)screen=true;else ++stats[base];}
                    else{hit(q,p!=0);if(p)break;}
                }
            }
        }else{
            const auto& list=topology.jumps[own==1?0:1][t][s];
            for(int i=0;i<list.count;++i){const auto d=list.dest[i];if(d.blocker==90 || !b[d.blocker])hit(d.square);}
        }
    }
    for(int i=0;i<count;++i){
        const int s=squares[i],v=b[s]*side>0?0:1,t=std::abs(b[s]),base=v*28+(t-1)*4;
        stats[base+1]+=attack[1-v][s]>0;stats[base+2]+=attack[v][s]>0;
        stats[base+3]+=attack[1-v][s] && (!attack[v][s] || cheapest[1-v][s]<value[t]);
        if(t==1){
            const int x=s%9,y=s/9;
            for(int a=-1;a<=1;++a)for(int c=-1;c<=1;++c)if(unsigned(x+a)<9 && unsigned(y+c)<10){
                int q=(y+c)*9+x+a;
                stats[56+v*4]+=std::min<int>(attack[1-v][q],3);
                stats[57+v*4]+=std::min<int>(attack[v][q],3);
                stats[58+v*4]+=!attack[1-v][q] && b[q]*(b[s]>0?1:-1)<=0;
            }
            stats[59+v*4]=attack[1-v][s]>0;
        }
    }
    for(int i=0;i<56;++i)out[i]=stats[i]*(i%4==0?.125f:.5f);
    for(int v=0;v<2;++v){out[56+v*4]=stats[56+v*4]/12.f;out[57+v*4]=stats[57+v*4]/12.f;out[58+v*4]=stats[58+v*4]/9.f;out[59+v*4]=float(stats[59+v*4]);}
}
}
