/* Optional host scaling capability; default 3DS UI stays unchanged. */
#include "../core/pd_settings.h"
#include "../core/pd_ui_internal.h"
#include <assert.h>
#include <stdio.h>
static uint32_t pixels[512*384];
static bool has(const struct pd_ui* ui,int id) {
    for(int i=0;i<ui->hitCount;i++)if(ui->hits[i].id==id)return true;
    return false;
}
static void tap(struct pd_ui* ui,int id) {
    for(int i=0;i<ui->hitCount;i++)if(ui->hits[i].id==id) {
        struct pd_hit h=ui->hits[i];pd_ui_touch(ui,h.x+1,h.y+1,true);pd_ui_touch(ui,h.x+1,h.y+1,false);return;
    }
    assert(!"no such hit");
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
    // EXPAND hosts: ASPECT opens a pick-list; AUTO ZOOM IN shows once it isn't
    // ORIGINAL and flips in place; a game EXPAND can't do shows ASPECT untappable.
    pd_canvas_init(&c,512,384,pixels);
    struct pd_host_info wide={.hideScreenMode=true,.hasExpand=true,.expandGame=true};
    pd_settings_defaults(&settings);ui.overlay=PD_OVERLAY_NONE;
    pd_ui_draw(&ui,&c,&game,&snap,NULL);assert(!has(&ui,HIT_SETTING+SETTING_ASPECT));
    pd_ui_draw(&ui,&c,&game,&snap,&wide);
    assert(has(&ui,HIT_SETTING+SETTING_ASPECT) && !has(&ui,HIT_SETTING+SETTING_AUTO_ZOOM));
    tap(&ui,HIT_SETTING+SETTING_ASPECT);assert(ui.overlay==PD_OVERLAY_PICK_ASPECT);
    pd_ui_draw(&ui,&c,&game,&snap,&wide);tap(&ui,HIT_OPTION+PD_ASPECT_ZOOM_OUT);
    assert(ui.overlay==PD_OVERLAY_NONE && settings.aspect==PD_ASPECT_ZOOM_OUT);
    assert(pd_ui_take_action(&ui)==PD_ACTION_SETTINGS_CHANGED);
    pd_ui_draw(&ui,&c,&game,&snap,&wide);assert(has(&ui,HIT_SETTING+SETTING_AUTO_ZOOM) && settings.autoZoom);
    tap(&ui,HIT_SETTING+SETTING_AUTO_ZOOM);assert(!settings.autoZoom);
    assert(pd_ui_take_action(&ui)==PD_ACTION_SETTINGS_CHANGED);
    wide.expandGame=false;pd_ui_draw(&ui,&c,&game,&snap,&wide);
    assert(!has(&ui,HIT_SETTING+SETTING_ASPECT) && !has(&ui,HIT_SETTING+SETTING_AUTO_ZOOM));
    // Saved and read back; a file from before (no aspect / autozoom keys) keeps the defaults.
    const char* path="/tmp/pd-host-display-settings.ini";
    assert(pd_settings_save(&settings,path));
    struct pd_settings back;assert(pd_settings_load(&back,path));
    assert(back.aspect==PD_ASPECT_ZOOM_OUT && !back.autoZoom && back.ffSpeed==settings.ffSpeed);
    FILE* old=fopen(path,"w");fputs("screen=1\nff=3\nguide=0\n",old);fclose(old);
    assert(pd_settings_load(&back,path) && back.aspect==PD_ASPECT_ORIGINAL && back.autoZoom && back.ffSpeed==3);
    remove(path);
    puts("PASS default/null host, optional scaling row, remaining settings and FF interaction, EXPAND rows");
}
