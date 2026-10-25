/* Optional host scaling capability; default 3DS UI stays unchanged. */
#include "../core/pd_ui_internal.h"
#include <assert.h>
#include <stdio.h>
static uint32_t pixels[512*384];
static bool has(const struct pd_ui* ui,int id) {
    for(int i=0;i<ui->hitCount;i++)if(ui->hits[i].id==id)return true;
    return false;
}
int main(void) {
    struct pd_settings settings={.ffSpeed=2};struct pd_ui ui;
    pd_ui_init(&ui,&settings);ui.tab=PD_TAB_SETTINGS;
    struct pd_snapshot snap={0};struct pd_game game={0};
    struct pd_host_info host={.hasState=true};struct pd_canvas c;
    for(int size=0;size<2;size++) {
        pd_canvas_init(&c,size?512:320,size?384:240,pixels);
        pd_ui_draw(&ui,&c,&game,&snap,NULL);assert(has(&ui,HIT_SETTING+SETTING_SCREEN));
        pd_ui_draw(&ui,&c,&game,&snap,&host);assert(has(&ui,HIT_SETTING+SETTING_SCREEN));
        host.hideScreenMode=true;pd_ui_draw(&ui,&c,&game,&snap,&host);
        assert(!has(&ui,HIT_SETTING+SETTING_SCREEN));
        for(int row=SETTING_FF;row<=SETTING_LEAVE;row++)assert(has(&ui,HIT_SETTING+row));
        for(int i=0;i<ui.hitCount;i++)if(ui.hits[i].id==HIT_SETTING+SETTING_FF) {
            struct pd_hit h=ui.hits[i];pd_ui_touch(&ui,h.x+1,h.y+1,true);pd_ui_touch(&ui,h.x+1,h.y+1,false);break;
        }
        assert(ui.overlay==PD_OVERLAY_PICK_FF);assert(pd_ui_back(&ui));host.hideScreenMode=false;
    }
    puts("PASS default/null host, optional scaling row, remaining settings and FF interaction");
}
