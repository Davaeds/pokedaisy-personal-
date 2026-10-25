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
static char liveName[48]="live";
static char liveSize[16]="";
static void shot(const char* name) {
  char path[1200];snprintf(path,sizeof path,"%s/%s-%s.png",output,liveName,name);
  assert(capture_frame(path,NULL));
}
static void drive(struct mCore* c, unsigned keys, int n) {
  c->setKeys(c,keys);while(n-->0)c->runFrame(c);c->setKeys(c,0);
}
static void test_frame(struct mCore* c,bool companion,int frame,struct pd_ui* ui,struct pd_snapshot* snap) {
  if(live) {
    assert(frame==0);
    bool isGba=c->platform(c)==mPLATFORM_GBA;
    // A copy: mGBA swaps its ROM mapping for a writable one once the game touches
    // the cartridge's GPIO registers (FireRed does), so the block's pointer goes stale.
    size_t len=0;const unsigned char* block=isGba?c->getMemoryBlock(c,0x08,&len):NULL;
    unsigned char* rom=malloc(len?len:1);assert(rom);if(len)memcpy(rom,block,len);
    struct pd_game game={0};
    if(isGba){pd_game_detect(&game,rom,len);game.rom=rom;game.romSize=len;}
    char crcName[16];uint32_t crc=0;c->checksum(c,&crc,mCHECKSUM_CRC32);snprintf(crcName,sizeof crcName,"%08x",crc);
    snprintf(liveName,sizeof liveName,"live-%s%s",isGba?game.code:crcName,liveSize);
    bool companionGame=isGba && game.cfg;
    // native-capture/boot/default.txt's way in: START, then A on CONTINUE and
    // through the recap pages, then B (FireRed's quest log), until the field
    // has been up for a second. The party alone isn't enough: Emerald reads it
    // from the save before its intro.
    struct pd_settings probe=*ui->settings;probe.aspect=PD_ASPECT_EXPAND;
    int fieldFrames=0,step=0;
    for(int f=0;f<6000 && fieldFrames<60;f++) {
      unsigned keys=0;
      if(f==120)keys=1<<3;                              // START
      else if(f>150 && fieldFrames==0 && f%30==0)keys=++step<=40?1:2; // A, then B
      c->setKeys(c,keys);c->runFrame(c);wide_tick(c,false,&probe);c->setKeys(c,0);
        bool field=wideSupported?pk_wide_scene()==PK_WIDE_SCENE_FIELD:f>1500;
      fieldFrames=field?fieldFrames+1:0;
    }
    assert(fieldFrames>=60);
    if(companionGame){
      pd_snapshot_read(snap,&game,read_memory,c);
      assert(snap->valid && snap->partyCount>0 && snap->posOk);
    }
    drive(c,0,120);pd_snapshot_read(snap,&game,read_memory,c);
    unsigned gw,gh;c->desiredVideoDimensions(c,&gw,&gh);
    present_game(gw,gh);shot("game");
    if(wideSupported) {
      // EXPAND and ZOOM OUT while walking: the window's middle must be the
      // game's own frame, pixel for pixel, on every frame it shows (frames the
      // game wrote VRAM in mid-draw can't be redrawn and don't count).
      struct pd_settings* s=ui->settings;int keep=s->aspect;
      int ow,oh;assert(!SDL_GetRendererOutputSize(renderer,&ow,&oh));
      unsigned fw=(unsigned)pk_wide_frame_w(),fh=(unsigned)pk_wide_frame_h();
      assert(fw==gw && fh==gh);
      int fieldShots=0;
      for(int a=PD_ASPECT_EXPAND;a<=PD_ASPECT_ZOOM_OUT;a++) {
        s->aspect=a;int shown=0,checked=0;
        for(int i=0;i<240;i++) {
          c->setKeys(c,(i/24)%2?1<<5:1<<4); // walk LEFT / RIGHT
          c->runFrame(c);wide_tick(c,false,s);
          if(!wideReady)continue;
          shown++;
          if(pk_wide_last_vram_torn())continue;
          if(!isGba && pk_wide_gb_last_torn())continue;
          checked++;
          int mx=(int)(wideW-fw)/2,my=(int)(wideH-fh)/2;
          for(unsigned y=0;y<fh;y++)for(unsigned x=0;x<fw;x++)
            assert(wideFrame[(y+my)*wideW+x+mx]==wideSource[y*fw+x]);
        }
        // Walking can step through a door: let the next map come up.
        c->setKeys(c,0);
        for(int i=0;i<600 && (i<30 || !wideReady);i++){c->runFrame(c);wide_tick(c,false,s);}
        fprintf(stderr,"%s %s: %ux%u window, %d of 240 frames expanded, %d checked exact (scene %d, mode %d, fault %d)\n",
                liveName,pd_aspect_name(a),wideW,wideH,shown,checked,pk_wide_scene(),pk_wide_last_mode(),pk_wide_fault());
        assert(shown>=30 && checked>=20);
        if(!wideReady) {
          // Walking found a wild Pokemon (or a menu): what was shown checked out.
          fprintf(stderr,"%s %s: left the field (scene %d)\n",liveName,pd_aspect_name(a),pk_wide_scene());
          continue;
        }
        fieldShots|=1<<a;
        unsigned w,h;wide_window(ow,oh,fw,fh,a,&w,&h);assert(w==wideW && h==wideH);
        present_game(gw,gh);shot(a==PD_ASPECT_EXPAND?"expand":"zoomout");
        // The window reaches the screen's edges: what's left is under two scale
        // steps (one for the division, one for keeping the margins even).
        int scale=lastGameRect.w/(int)wideW;
        assert(scale>=1 && lastGameRect.h==(int)wideH*scale);
        assert(ow-lastGameRect.w<2*scale || wideW>=PK_WIDE_MAX_VIEW_W);
        assert(oh-lastGameRect.h<2*scale || wideH>=PK_WIDE_MAX_VIEW_H);
      }
      assert(fieldShots); // at least one mode was checked through to its screenshot
      // The game menu over the expanded picture: its window, the game around it.
      static const char* items[]={"Resume game","Party","Save state","Load state","Quit to game list"};
      struct pd_menu m;pd_menu_init(&m,"GAME",items,5);
      s->aspect=PD_ASPECT_EXPAND;for(int i=0;i<4;i++){c->runFrame(c);wide_tick(c,false,s);}
      present_menu(&m,gw,gh);shot("menu");
      assert(lastGameRect.w>=ow-2*(lastGameRect.w/(int)wideW) && wideReady);
      s->aspect=keep;wideReady=false;
    }
    if(!companionGame) {
      free(rom);
      printf("PASS %s: boots to the field, emulation%s\n",liveName,wideSupported?" and EXPAND":" only");
      running=0;return;
    }
    struct pd_host_info host={0};ui->tab=PD_TAB_PARTY;
    pd_ui_draw(ui,&canvas,&game,snap,&host);present();shot("party");
    bool found=false;
    for(int i=0;i<ui->hitCount;i++)if(ui->hits[i].id==HIT_SLOT) {
      struct pd_hit h=ui->hits[i];pd_ui_touch(ui,h.x+1,h.y+1,true);pd_ui_touch(ui,h.x+1,h.y+1,false);found=true;break;
    }
    assert(found && ui->summarySlot==0);
    pd_ui_draw(ui,&canvas,&game,snap,&host);present();shot("summary");
    assert(pd_ui_back(ui) && ui->summarySlot==-1);
    uint32_t before=c->frameCounter(c);
    assert(state(c,"integration.ss",true));drive(c,0,20);assert(state(c,"integration.ss",false));
    assert(c->frameCounter(c)==before);unlink("integration.ss");
    free(rom);
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
// The EXPAND window on common handheld panels: the game's own scale, the
// screen's shape, even margins; ZOOM OUT one scale less.
static void test_wide_window(void) {
  static const struct {int ow,oh;unsigned fw,fh;int aspect;unsigned w,h;} cases[]={
    {1024,768,240,160,PD_ASPECT_EXPAND,256,192}, // TrimUI Brick: 4x
    {1280,720,240,160,PD_ASPECT_EXPAND,320,180}, // Smart Pro: 4x, full screen
    {1280,720,240,160,PD_ASPECT_ZOOM_OUT,426,240},
    {640,480,240,160,PD_ASPECT_EXPAND,320,240},  // RG35XX / RGB20 class: 2x
    {720,720,240,160,PD_ASPECT_EXPAND,240,240},  // RGB30 / Cube: 3x, square
    {750,560,240,160,PD_ASPECT_EXPAND,250,186},  // Miyoo Mini v4: 3x
    {960,544,240,160,PD_ASPECT_EXPAND,320,180},  // RG503: 3x
    {1920,1080,240,160,PD_ASPECT_EXPAND,320,180},// RG556 / desktop: 6x
    {1920,1080,240,160,PD_ASPECT_ZOOM_OUT,384,216},
    {320,240,240,160,PD_ASPECT_ZOOM_OUT,320,240},// 1x can't go lower
    {1280,720,160,144,PD_ASPECT_EXPAND,256,144}, // Game Boy: 5x
    {1024,768,160,144,PD_ASPECT_EXPAND,204,152},
    {4096,2160,240,160,PD_ASPECT_ZOOM_OUT,340,180},
  };
  for(unsigned i=0;i<sizeof cases/sizeof*cases;i++) {
    unsigned w,h;wide_window(cases[i].ow,cases[i].oh,cases[i].fw,cases[i].fh,cases[i].aspect,&w,&h);
    if(w!=cases[i].w || h!=cases[i].h)fprintf(stderr,"%dx%d: %ux%u\n",cases[i].ow,cases[i].oh,w,h);
    assert(w==cases[i].w && h==cases[i].h);
    assert((w-cases[i].fw)%2==0 && (h-cases[i].fh)%2==0);
  }
}
// The companion's canvas: the largest whole scale keeping 320x240.
static void test_companion_size(void) {
  static const struct {int ow,oh,w,h;} cases[]={
    {640,480,320,240},{1280,720,426,240},{1024,768,341,256},{1920,1080,480,270},{720,720,360,360},
    {750,560,375,280},{960,544,480,272},{512,384,512,384},{320,240,320,240},{300,200,320,240},
  };
  for(unsigned i=0;i<sizeof cases/sizeof*cases;i++) {
    int w,h;companion_size(cases[i].ow,cases[i].oh,&w,&h);
    if(w!=cases[i].w||h!=cases[i].h)fprintf(stderr,"%dx%d: %dx%d\n",cases[i].ow,cases[i].oh,w,h);
    assert(w==cases[i].w&&h==cases[i].h);
  }
}
// A pad plugged in and out while running (SDL's virtual joystick, which SDL
// maps as a game controller): opened on its own, its buttons are the game's
// (SDL's B, the right face button, is the game's A), and pulling it lets go.
static void test_controller_hotplug(void) {
#if SDL_VERSION_ATLEAST(2, 24, 0)
  assert(!SDL_Init(SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER));
  controller=NULL;held=pressed=0;
  SDL_VirtualJoystickDesc desc={.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION,.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER,
    .naxes=SDL_CONTROLLER_AXIS_MAX,.nbuttons=SDL_CONTROLLER_BUTTON_MAX,.name="PokeDaisy test pad"};
  int index=SDL_JoystickAttachVirtualEx(&desc);assert(index>=0);
  events();assert(controller);
  SDL_Joystick* js=SDL_GameControllerGetJoystick(controller);
  assert(!SDL_JoystickSetVirtualButton(js,SDL_CONTROLLER_BUTTON_B,1));
  events();assert(down(A) && (held&(1u<<A)));
  assert(!SDL_JoystickSetVirtualAxis(js,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767));
  events();assert(held&(1u<<R2));
  assert(!SDL_JoystickDetachVirtual(index));
  events();assert(!controller && !held);
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER);
  puts("PASS controller plugged in and out, its buttons the game's");
#endif
}
int main(int argc,char** argv) {
  test_wide_window();
  test_companion_size();
  if(argc<4)test_controller_hotplug();
  if(argc==4 || argc==5) {
    // ROM DATA OUT [WxH,WxH,...]: once per window size (default 512x384).
    char sizes[256];snprintf(sizes,sizeof sizes,"%s",argc==5?argv[4]:"");
    char* next=sizes;int result=0;
    do {
      char* size=strsep(&next,",");
      if(*size){setenv("POCKET_WINDOW",size,1);snprintf(liveSize,sizeof liveSize,"-%s",size);}
      live=true;output=argv[3];running=1;
      char* args[]={"pokedaisy-linux","--data",argv[2],"--frames","1",argv[1],NULL};
      result|=linux_app_main(6,args);
    } while(next);
    return result;
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
