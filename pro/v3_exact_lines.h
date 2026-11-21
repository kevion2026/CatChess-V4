#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace v3lines {
// Exact Xiangqi ray occlusion, cannon screens, horse legs and elephant eyes.
// These are shared structural inputs; no hand-written evaluation score is added.
inline void lineGeometry(const int8_t* b,int side,float* out) {
    int attack[2][90]{}, cheapest[2][90],kingZone[2][3]{};
    for(auto& view:cheapest) std::fill_n(view,90,30000);
    std::fill_n(out,64,0.f);
    constexpr int value[8]={0,20000,140,140,390,800,390,90};
    auto inside=[](int x,int y){return x>=0 && x<9 && y>=0 && y<10;};
    auto palace=[](int x,int y,int own){return x>=3 && x<=5 && (own==1?y>=7:y<=2);};
    constexpr int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
    for(int s=0;s<90;++s) if(b[s]) {
        int piece=b[s],own=piece>0?1:-1,v=piece*side>0?0:1,t=std::abs(piece),x=s%9,y=s/9;
        auto hit=[&](int a,int c,bool mobility=true) {
            if(!inside(a,c)) return;
            int q=c*9+a; ++attack[v][q]; cheapest[v][q]=std::min(cheapest[v][q],value[t]);
            if(mobility && b[q]*own<=0) out[v*28+(t-1)*4]+=1.f/8.f;
        };
        if(t==5 || t==6) {
            for(int d=0;d<4;++d) {
                int a=x+dx[d],c=y+dy[d],screens=0;
                while(inside(a,c)) {
                    int p=b[c*9+a];
                    if(t==5) {hit(a,c); if(p) break;}
                    else if(!screens) {
                        if(p) screens=1;
                        else out[v*28+(t-1)*4]+=1.f/8.f;
                    } else {
                        // Empty squares beyond one screen are controlled too:
                        // a king moving there would become the captured piece.
                        hit(a,c,p!=0);
                        if(p) break;
                    }
                    a+=dx[d];c+=dy[d];
                }
            }
        } else if(t==4) {
            constexpr int hx[8]={1,2,2,1,-1,-2,-2,-1},hy[8]={2,1,-1,-2,-2,-1,1,2};
            for(int k=0;k<8;++k) {
                int a=x+hx[k],c=y+hy[k];
                if(inside(a,c) && !b[(y+(std::abs(hy[k])==2?hy[k]/2:0))*9+x+(std::abs(hx[k])==2?hx[k]/2:0)]) hit(a,c);
            }
        } else if(t==7) {
            hit(x,y-own);
            if(own==1?y<=4:y>=5) {hit(x-1,y);hit(x+1,y);}
        } else if(t==1) {
            for(int d=0;d<4;++d) if(inside(x+dx[d],y+dy[d]) && palace(x+dx[d],y+dy[d],own)) hit(x+dx[d],y+dy[d]);
        } else {
            for(int a:{-1,1}) for(int c:{-1,1}) {
                int xx=x+a*(t==3?2:1),yy=y+c*(t==3?2:1);
                if(!inside(xx,yy)) continue;
                if(t==2) {if(palace(xx,yy,own)) hit(xx,yy);}
                else if((own==1?yy>=5:yy<=4) && !b[(y+c)*9+x+a]) hit(xx,yy);
            }
        }
    }
    for(int s=0;s<90;++s) if(b[s]) {
        int v=b[s]*side>0?0:1,t=std::abs(b[s]),base=v*28+(t-1)*4;
        out[base+1]+=attack[1-v][s]>0?.5f:0.f;
        out[base+2]+=attack[v][s]>0?.5f:0.f;
        out[base+3]+=(attack[1-v][s] && (!attack[v][s] || cheapest[1-v][s]<value[t]))?.5f:0.f;
        if(t==1) {
            int x=s%9,y=s/9;
            for(int a=-1;a<=1;++a) for(int c=-1;c<=1;++c) if(inside(x+a,y+c)) {
                int q=(y+c)*9+x+a;
                kingZone[v][0]+=std::min(attack[1-v][q],3);
                kingZone[v][1]+=std::min(attack[v][q],3);
                if(!attack[1-v][q] && b[q]*(b[s]>0?1:-1)<=0) ++kingZone[v][2];
            }
            out[59+v*4]=attack[1-v][s]>0?1.f:0.f;
        }
    }
    for(int v=0;v<2;++v){
        out[56+v*4]=kingZone[v][0]/12.f;
        out[57+v*4]=kingZone[v][1]/12.f;
        out[58+v*4]=kingZone[v][2]/9.f;
    }
}
}
