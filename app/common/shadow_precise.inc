REXCVAR_DEFINE_BOOL(ufc_shadow_precise,false,"Tests","Bounded consecutive shadow trace; diagnostic overhead, no performance claims");
namespace {
std::atomic<uint64_t> precise_serial{0},precise_reset{0};
uint32_t PW(uint8_t* b,uint32_t p,uint32_t off){return p?W(b,p+off):0;}
void Precise(const char* event,PPCContext& c,uint8_t* b,uint32_t object,uint32_t caller,uint32_t detail){
 if(!REXCVAR_GET(ufc_shadow_precise))return;
 const auto serial=precise_serial.fetch_add(1)+1;
 if(serial>30000)return;
 const auto m=W(b,0x833C810C),st=ShadowThreadState(c,b);
 REXLOG_INFO("[shadow-precise] seq={} reset={} event={} object={:08X} caller={:08X} detail={:08X} manager={:08X} mode={} enabled={} gate={} count={} list={:08X},{:08X},{:08X},{:08X} state={:08X} map={:08X} use={} context={:08X}",serial,precise_reset.load(),event,object,caller,detail,m,PW(b,m,24),PW(b,m,16),W(b,0x8333A0BC),PW(b,m,996),PW(b,m,956),PW(b,m,960),PW(b,m,964),PW(b,m,968),st,PW(b,st,2336),PW(b,st,2340),c.r4.u32);
 if(serial==30000)REXLOG_INFO("[shadow-precise] hard event limit reached; trace stops");
}
}

extern "C" void __imp__sub_82DDF018(PPCContext&,uint8_t*);
REX_HOOK_RAW(sub_82DDF018){
 const auto s=ctx.r3.u32,lr=uint32_t(ctx.lr);
 if(REXCVAR_GET(ufc_shadow_precise)&&precise_serial.load()<30000)REXLOG_INFO("[shadow-receiver] self={:08X} ready={} model64={:08X} model76={:08X} vtable={:08X}",s,PW(base,s,32),PW(base,s,64),PW(base,s,76),PW(base,s,0));
 Precise("receiver-enter",ctx,base,s,lr);
 __imp__sub_82DDF018(ctx,base);
 Precise("receiver-leave",ctx,base,s,lr);
}
extern "C" void __imp__sub_8275DAF0(PPCContext&,uint8_t*);
REX_HOOK_RAW(sub_8275DAF0){
 const auto source=ctx.r3.u32,dest=ctx.r4.u32,lr=uint32_t(ctx.lr);
 const bool trace=REXCVAR_GET(ufc_shadow_precise)&&precise_serial.load()<30000;
 if(trace)REXLOG_INFO("[shadow-state-copy] source={:08X} dest={:08X} source-map={:08X} source-use={} dest-map={:08X} dest-use={}",source,dest,PW(base,source,2336),PW(base,source,2340),PW(base,dest,2336),PW(base,dest,2340));
 __imp__sub_8275DAF0(ctx,base);
 if(trace)Precise("state-copied",ctx,base,dest,lr,PW(base,dest,2340));
}
extern "C" void __imp__sub_82768CE0(PPCContext&,uint8_t*);
REX_HOOK_RAW(sub_82768CE0){
 const auto st=ctx.r3.u32,lr=uint32_t(ctx.lr);
 __imp__sub_82768CE0(ctx,base);
 static thread_local uint64_t last_zero=~uint64_t(0);
 const auto reset=precise_reset.load();
 if(ctx.r3.u32||last_zero!=reset){last_zero=reset;Precise("flag-read",ctx,base,st,lr,ctx.r3.u32);}
}
extern "C" void __imp__sub_8276A5B0(PPCContext&,uint8_t*);
REX_HOOK_RAW(sub_8276A5B0){
 const auto s=ctx.r3.u32,lr=uint32_t(ctx.lr);
 Precise("bind-slot12",ctx,base,s,lr);
 __imp__sub_8276A5B0(ctx,base);
}
extern "C" void __imp__sub_82EEB290(PPCContext&,uint8_t*);
REX_HOOK_RAW(sub_82EEB290){
 const auto s=ctx.r3.u32,lr=uint32_t(ctx.lr),r4=ctx.r4.u32,r6=ctx.r6.u32;
 Precise("coordinate",ctx,base,s,lr,(r4<<16)|r6);
 __imp__sub_82EEB290(ctx,base);
}

// Isolated A/B experiment for all consumers of projection constants137..144. Off by default; no shared manager writes.
REXCVAR_DEFINE_BOOL(ufc_shadow_flip_receiver_y,false,"Tests","Experimental receiver-only shadow projection Y inversion");
REXCVAR_DEFINE_BOOL(ufc_shadow_receiver_positive,false,"Tests","Diagnostic positive control: sample brightest map texel across receiver");
// 8-bit tiled addressing adapted from the SDK BSD-licensed texture/util.cpp.
static unsigned ShadowTile8(unsigned x,unsigned y){unsigned macro=((x>>5)+(y>>5)*8)<<7;unsigned micro=(x&7)+((y&14)<<2);unsigned off=macro+((micro&~15)<<1)+(micro&15)+((y&1)<<4);return ((off&~511)<<3)+((y&16)<<7)+((off&448)<<2)+(((((y&8)>>2)+(x>>3))&3)<<6)+(off&63);}
extern "C" void __imp__sub_8274D4C0(PPCContext&,uint8_t*);
REX_HOOK_RAW(sub_8274D4C0){
 const auto manager=ctx.r3.u32,caller=uint32_t(ctx.lr);
 const bool receiver=ctx.r5.u32==137&&ctx.r6.u32==0;
 static std::atomic<uint64_t> coverage{0};
 if(receiver&&(REXCVAR_GET(ufc_shadow_flip_receiver_y)||REXCVAR_GET(ufc_shadow_receiver_positive))){
  const auto sp=ctx.r1.u32;
  rex::StackFrame frame(ctx,1456);
  REX_STORE_U32(ctx.r1.u32,sp);
  const auto clone=ctx.r1.u32+256;
  std::memcpy(base+clone,base+manager,1200);
  // Four consecutive uploaded float4 rows; c138 computes projected Y.
  if(REXCVAR_GET(ufc_shadow_receiver_positive)){
   const auto map=W(base,manager+932);const auto fw=map?W(base,map+32):0;
   if(map&&W(base,map+36)==0x001FE0FF&&(fw&63)==2){
    const uint32_t phys=((fw&0x1FFFF000u)+0x1000u)&0x1FFFF000u;
    const auto* data=base+0x100000000ULL+phys;
    unsigned best=0,bx=0,by=0;
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){const auto value=data[ShadowTile8(x,y)];if(value>best){best=value;bx=x;by=y;}}
    const float matrix[16]={0,0,0,2*((bx+0.5f)/256)-1,0,0,0,1-2*((by+0.5f)/256),0,0,1,0,0,0,0,1};
    for(unsigned i=0;i<16;++i){uint32_t bits;std::memcpy(&bits,&matrix[i],4);REX_STORE_U32(clone+1008+i*4,bits);}
    if(coverage.load()<4)REXLOG_INFO("[shadow-positive] map={:08X} phys={:08X} texel={},{} value={}",map,phys,bx,by,best);
   }
  }else{for(unsigned off=1024;off<1040;off+=4){auto bits=W(base,clone+off)^0x80000000u;REX_STORE_U32(clone+off,bits);}}
  ctx.r3.u64=clone;
  const auto n=++coverage;
  if(n<=4||(n&(n-1))==0)REXLOG_INFO("[shadow-projection-ab] variant=projection-control n={} caller={:08X} original={:08X} clone={:08X}",n,caller,manager,clone);
  __imp__sub_8274D4C0(ctx,base);
 }else{
  __imp__sub_8274D4C0(ctx,base);
 }
}
