/* Exercise the actual SDL/mGBA host. No fixture injection into emulated RAM. */
#define POCKET_TEST
#define main linux_app_main
#include "main.c"
#undef main
#include <assert.h>
static bool gba, live;
static uint32_t pausedAt;
static const char* output;
static void tap(SDL_Keycode code) {
  SDL_Event e={.type=SDL_KEYDOWN};e.key.keysym.sym=code;
  assert(SDL_PushEvent(&e)==1);e.type=SDL_KEYUP;assert(SDL_PushEvent(&e)==1);
}
static void shot(const char* name) {
  char path[1200];snprintf(path,sizeof path,"%s/%s.png",output,name);
  assert(capture_frame(path,NULL));
}
static void drive(struct mCore* c, unsigned keys, int n) {
  c->setKeys(c,keys);while(n-->0)c->runFrame(c);c->setKeys(c,0);
}
static void test_frame(struct mCore* c,bool companion,int frame,struct pd_ui* ui,struct pd_snapshot* snap) {
  if(live) {
    assert(frame==0 && c->platform(c)==mPLATFORM_GBA);
    size_t len=0;const unsigned char* rom=c->getMemoryBlock(c,0x08,&len);
    struct pd_game game;pd_game_detect(&game,rom,len);game.rom=rom;game.romSize=len;
    for(int i=0;i<120 && (!snap->valid || snap->partyCount==0 || !snap->posOk);i++) {
      drive(c,1,8);drive(c,0,40);pd_snapshot_read(snap,&game,read_memory,c);
    }
    assert(snap->valid && snap->partyCount>0 && snap->posOk);
    drive(c,0,120);pd_snapshot_read(snap,&game,read_memory,c);
    present_game(240,160);shot("live-emerald-game");
    struct pd_host_info host={0};ui->tab=PD_TAB_PARTY;
    pd_ui_draw(ui,&canvas,&game,snap,&host);present();shot("live-emerald-party");
    bool found=false;
    for(int i=0;i<ui->hitCount;i++)if(ui->hits[i].id==HIT_SLOT) {
      struct pd_hit h=ui->hits[i];pd_ui_touch(ui,h.x+1,h.y+1,true);pd_ui_touch(ui,h.x+1,h.y+1,false);found=true;break;
    }
    assert(found && ui->summarySlot==0);
    pd_ui_draw(ui,&canvas,&game,snap,&host);present();shot("live-emerald-summary");
    assert(pd_ui_back(ui) && ui->summarySlot==-1);
    uint32_t before=c->frameCounter(c);
    assert(state(c,"integration.ss",true));drive(c,0,20);assert(state(c,"integration.ss",false));
    assert(c->frameCounter(c)==before);unlink("integration.ss");
    printf("PASS real GBA save, party=%d, companion Summary/back and state reload\n",snap->partyCount);
    running=0;return;
  }
  assert(c->platform(c)==(gba?mPLATFORM_GBA:mPLATFORM_GB));
  if(frame==0) {
    assert(!companion && lastGameRect.w>0 && lastGameRect.h>0);
    pausedAt=c->frameCounter(c);tap(SDLK_TAB);
  } else if(frame==1) {
    assert(companion && c->frameCounter(c)==pausedAt);
    if(audio)assert(SDL_GetQueuedAudioSize(audio)==0);
    tap(SDLK_x);
  } else if(frame==2) {
    assert(!companion && c->frameCounter(c)==pausedAt);
    uint32_t before=c->frameCounter(c);
    assert(state(c,"integration.ss",true));drive(c,0,12);assert(state(c,"integration.ss",false));
    assert(c->frameCounter(c)==before);unlink("integration.ss");
    tap(SDLK_ESCAPE);
  } else if(frame==3) {
    assert(companion);tap(SDLK_x);
  } else if(frame==4) {
    assert(!companion);
    assert(keyboard(SDLK_z)==A && joy(0)==B && joy(1)==A);
    assert(controller_button(SDL_CONTROLLER_BUTTON_B)==A);
    assert(controller_button(SDL_CONTROLLER_BUTTON_A)==B);
    // Unknown raw devices must not inherit the TrimUI button indices.
    controller=NULL;rawTrimui=false;held=pressed=0;
    SDL_Event e={.type=SDL_JOYBUTTONDOWN};e.jbutton.button=1;
    assert(SDL_PushEvent(&e)==1);events();assert(!held && !pressed);
    rawTrimui=true;assert(SDL_PushEvent(&e)==1);events();assert(down(A));held=pressed=0;
  } else if(frame==5)assert(c->frameCounter(c)>pausedAt);
}
int main(int argc,char** argv) {
  if(argc==4) {
    live=true;output=argv[3];running=1;
    char* args[]={"pokedaisy-linux","--data",argv[2],"--frames","1",argv[1],NULL};
    return linux_app_main(6,args);
  }
  char dir[]="/tmp/pd-linux-runtime-XXXXXX";assert(mkdtemp(dir));
  char* args[]={"pokedaisy-linux","--data",dir,"--frames","8","diagnostic.gb",NULL};
  assert(!linux_app_main(6,args));
  char path[1200];snprintf(path,sizeof path,"%s/saves/diagnostic.gb.sav",dir);
  FILE* f=fopen(path,"rb");assert(f && fgetc(f)==0x5a);fclose(f);
  gba=true;running=1;audio=0;messageUntil=0;held=pressed=0;args[5]="diagnostic.gba";
  assert(!linux_app_main(6,args));
  puts("PASS actual GB/GBA diagnostic execution, audio pause, SDL input, menus, state roundtrip and battery save");
  return 0;
}
