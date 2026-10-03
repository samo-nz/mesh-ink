#include "board/t5_packed_framebuffer.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace meshink_t5_packed;

static uint8_t read_gray4(const uint8_t* fb,int width,int x,int y) {
    const size_t row_bytes=((size_t)width+1U)/2U;
    const uint8_t packed=fb[(size_t)y*row_bytes+((unsigned)x>>1)];
    return (x&1)?(uint8_t)(packed>>4):(uint8_t)(packed&0x0FU);
}

static void write_gray4(uint8_t* fb,int width,int x,int y,uint8_t gray4) {
    const size_t row_bytes=((size_t)width+1U)/2U;
    uint8_t& packed=fb[(size_t)y*row_bytes+((unsigned)x>>1)];
    if(x&1)packed=(uint8_t)((packed&0x0FU)|((gray4&0x0FU)<<4));
    else packed=(uint8_t)((packed&0xF0U)|(gray4&0x0FU));
}

static bool reference_map_point(MeshInkRotation rotation,int pw,int ph,
                                int lx,int ly,int& px,int& py) {
    const bool portrait=rotation==MeshInkRotation::Portrait||
                        rotation==MeshInkRotation::InvertedPortrait;
    const int lw=portrait?ph:pw;
    const int lh=portrait?pw:ph;
    if(lx<0||ly<0||lx>=lw||ly>=lh)return false;
    switch(rotation) {
        case MeshInkRotation::Portrait:
            px=pw-1-ly;py=lx;break;
        case MeshInkRotation::InvertedLandscape:
            px=pw-1-lx;py=ph-1-ly;break;
        case MeshInkRotation::InvertedPortrait:
            px=ly;py=ph-1-lx;break;
        case MeshInkRotation::Landscape:
        default:
            px=lx;py=ly;break;
    }
    return true;
}

static void run_case(MeshInkRotation rotation,MeshInkRect rect,uint8_t gray4) {
    constexpr int pw=10,ph=6;
    constexpr size_t bytes=(size_t)pw*ph/2U;
    uint8_t actual[bytes],expected[bytes];
    memset(actual,0xA5,sizeof(actual));
    memset(expected,0xA5,sizeof(expected));

    const bool wrote=fill_logical_gray4(actual,pw,ph,rotation,rect,gray4);

    const bool portrait=rotation==MeshInkRotation::Portrait||
                        rotation==MeshInkRotation::InvertedPortrait;
    const int lw=portrait?ph:pw;
    const int lh=portrait?pw:ph;
    bool expected_write=false;
    for(int ly=0;ly<lh;++ly)for(int lx=0;lx<lw;++lx) {
        const long long rx1=(long long)rect.x+rect.width;
        const long long ry1=(long long)rect.y+rect.height;
        if(rect.width<=0||rect.height<=0||
           lx<rect.x||ly<rect.y||lx>=rx1||ly>=ry1)continue;
        int px=0,py=0;
        assert(reference_map_point(rotation,pw,ph,lx,ly,px,py));
        write_gray4(expected,pw,px,py,gray4);
        expected_write=true;
    }

    assert(wrote==expected_write);
    assert(memcmp(actual,expected,sizeof(actual))==0);
}


static int reference_row_inset(int radius,int row) {
    if(radius<=0||row<0||row>=radius)return 0;
    const int yy=radius-1-row;
    int inset=0;
    while(inset<radius) {
        const int xx=radius-inset;
        if(xx*xx+yy*yy<=radius*radius)break;
        ++inset;
    }
    return inset;
}

static bool reference_rounded_contains(MeshInkRect rect,int radius,int x,int y) {
    if(rect.width<=0||rect.height<=0)return false;
    int r=radius;
    if(r<0)r=0;
    const int max_radius=(rect.width<rect.height?rect.width:rect.height)/2;
    if(r>max_radius)r=max_radius;
    if(x<rect.x||y<rect.y||x>=rect.x+rect.width||y>=rect.y+rect.height)
        return false;
    if(!r)return true;
    const int local_y=y-rect.y;
    int row=-1;
    if(local_y<r)row=local_y;
    else if(local_y>=rect.height-r)row=rect.height-1-local_y;
    if(row<0)return true;
    const int inset=reference_row_inset(r,row);
    const int local_x=x-rect.x;
    return local_x>=inset&&local_x<rect.width-inset;
}

static void run_rounded_case(
    MeshInkRotation rotation,MeshInkRect rect,int radius,uint8_t gray4) {
    constexpr int pw=18,ph=14;
    constexpr size_t bytes=(size_t)pw*ph/2U;
    uint8_t actual[bytes],expected[bytes];
    memset(actual,0xA5,sizeof(actual));
    memset(expected,0xA5,sizeof(expected));

    const bool wrote=fill_logical_rounded_gray4(
        actual,pw,ph,rotation,rect,radius,gray4);

    const bool portrait=rotation==MeshInkRotation::Portrait||
                        rotation==MeshInkRotation::InvertedPortrait;
    const int lw=portrait?ph:pw;
    const int lh=portrait?pw:ph;
    bool expected_write=false;
    for(int ly=0;ly<lh;++ly)for(int lx=0;lx<lw;++lx) {
        if(!reference_rounded_contains(rect,radius,lx,ly))continue;
        int px=0,py=0;
        assert(reference_map_point(rotation,pw,ph,lx,ly,px,py));
        write_gray4(expected,pw,px,py,gray4);
        expected_write=true;
    }

    assert(wrote==expected_write);
    assert(memcmp(actual,expected,sizeof(actual))==0);
}

int main() {
    const MeshInkRotation rotations[]={
        MeshInkRotation::Landscape,
        MeshInkRotation::Portrait,
        MeshInkRotation::InvertedLandscape,
        MeshInkRotation::InvertedPortrait
    };
    const MeshInkRect rects[]={
        {0,0,1,1},{1,1,3,2},{2,0,5,4},{0,0,10,6},
        {-2,-1,5,4},{8,4,5,5},{-20,-20,3,3},{3,2,0,4}
    };
    for(const auto rotation:rotations)
        for(const auto rect:rects) {
            run_case(rotation,rect,0x0);
            run_case(rotation,rect,0x6);
            run_case(rotation,rect,0xF);
        }

    const MeshInkRect rounded_rects[]={
        {0,0,14,18},{1,2,11,13},{2,1,8,8},{-2,-3,10,12},{10,13,8,8}
    };
    const int radii[]={0,1,2,3,5,20};
    for(const auto rotation:rotations)
        for(const auto rect:rounded_rects)
            for(const int radius:radii) {
                run_rounded_case(rotation,rect,radius,0x0);
                run_rounded_case(rotation,rect,radius,0x7);
                run_rounded_case(rotation,rect,radius,0xF);
            }

    // Explicitly verify nibble ownership for odd and even physical X.
    uint8_t fb[5]={0xA5,0xA5,0xA5,0xA5,0xA5};
    fill_physical_gray4(fb,10,1,{1,0,1,1},0x3);
    assert(read_gray4(fb,10,0,0)==0x5);
    assert(read_gray4(fb,10,1,0)==0x3);
    fill_physical_gray4(fb,10,1,{2,0,1,1},0xC);
    assert(read_gray4(fb,10,2,0)==0xC);
    assert(read_gray4(fb,10,3,0)==0xA);

    std::puts("PASS: T5 packed 4bpp rectangle/rounded fills match pixel reference");
    return 0;
}
