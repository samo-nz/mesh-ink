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

    // Explicitly verify nibble ownership for odd and even physical X.
    uint8_t fb[5]={0xA5,0xA5,0xA5,0xA5,0xA5};
    fill_physical_gray4(fb,10,1,{1,0,1,1},0x3);
    assert(read_gray4(fb,10,0,0)==0x5);
    assert(read_gray4(fb,10,1,0)==0x3);
    fill_physical_gray4(fb,10,1,{2,0,1,1},0xC);
    assert(read_gray4(fb,10,2,0)==0xC);
    assert(read_gray4(fb,10,3,0)==0xA);

    std::puts("PASS: T5 packed 4bpp rectangle mapping and fills match pixel reference");
    return 0;
}
