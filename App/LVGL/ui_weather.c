#include "ui_weather.h"
#include "weather_service.h"
#include "weather_config.h"
#include "wifi_service_port.h"
#include "ui_fonts.h"
#include "ui_images.h"
#include <stdio.h>
#include <string.h>

#define BG 0xF5F7FA
#define TEXT 0x243247
#define MUTED 0x69788B
#define BORDER 0xE4EAF1
#define CURRENT_ACCENT 0x2563A6
#define TEMPERATURE_ACCENT 0xB85B2D
#define NIGHT_TEXT 0x586B87

/* 三天预报的日期使用蓝色、琥珀色、绿色，卡片保持原来的白色背景。 */
static const uint32_t forecast_accents[3] = {0x2563A6, 0x9B5D1D, 0x24745D};
/* 天气页面上下文，由UI任务维护；页面删除时释放定时器并清零。
 * 预报控件数组中，相同下标对应同一条预报记录，最多显示3天。
 */
typedef struct {
    lv_obj_t *root;           // 页面根容器，NULL表示页面未创建或已关闭
    lv_obj_t *refresh;        // 刷新按钮，请求进行中时禁用
    lv_obj_t *city;           // 当前城市名称标签
    lv_obj_t *temperature;    // 当前实况温度标签，单位为摄氏度
    lv_obj_t *text;           // 当前天气描述标签，例如晴、多云、小雨
    lv_obj_t *updated;        // 实况数据更新时间标签，来自API的last_update
    lv_obj_t *icon;           // 当前实况天气图标
    lv_obj_t *status;         // 底部状态标签，显示更新、断网或错误信息
    lv_obj_t *cards[3];       // 各天预报卡片容器，超出有效预报条数的卡片隐藏
    lv_obj_t *dates[3];       // 各天日期标签，根据实际日期显示今天、明天、后天等
    lv_obj_t *day_text[3];    // 各天白天天气描述标签
    lv_obj_t *night_text[3];  // 各天夜间天气描述标签
    lv_obj_t *ranges[3];      // 各天最低至最高温度标签
    lv_obj_t *winds[3];       // 各天预报风况标签，优先显示风向及风力等级
    lv_obj_t *day_icons[3];   // 各天白天天气图标
    lv_obj_t *night_icons[3]; // 各天夜间天气图标
    lv_timer_t *timer;        // 每200ms检查界面更新，页面关闭时删除
    uint32_t version;        // 上次绘制的天气快照版本，UINT32_MAX用于强制首次绘制
    uint32_t last_second;    // 上次检查时的LVGL运行秒数，用于限制日期及过期检查频率
    bool last_stale;         // 上次绘制时是否存在已过期的有效实况或预报数据
    char today[11];          // 上次检查的本机日期，格式YYYY-MM-DD；无有效日期时为空串
} ui_weather_context_t;
static ui_weather_context_t weather_ui;

static lv_obj_t *panel(lv_obj_t *parent,bool card)
{
    lv_obj_t *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj,card?8:0,0);lv_obj_set_style_pad_gap(obj,6,0);
    if(card) {
        lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_bg_color(obj,lv_color_white(),0);
        lv_obj_set_style_border_width(obj,1,0);lv_obj_set_style_border_color(obj,lv_color_hex(BORDER),0);
        lv_obj_set_style_radius(obj,8,0);
    }
    return obj;
}
static lv_obj_t *label(lv_obj_t *parent,const char *text,uint32_t color)
{
    lv_obj_t *obj=lv_label_create(parent);lv_label_set_text(obj,text);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);return obj;
}
static void root_deleted(lv_event_t *event)
{
    if(lv_event_get_target(event)!=weather_ui.root)return;
    if(weather_ui.timer)lv_timer_del(weather_ui.timer);
    memset(&weather_ui,0,sizeof(weather_ui));
}
static void close_clicked(lv_event_t *event) {(void)event;lv_obj_del(weather_ui.root);}
static void refresh_clicked(lv_event_t *event)
{
    (void)event;
    if(!weather_service_request_refresh(lv_tick_get()))
        lv_label_set_text(weather_ui.status,weather_service_get_snapshot()->connected?"稍后再试":"网络未连接");
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,lv_palette_t pressed,lv_event_cb_t callback)
{
    lv_obj_t *obj=lv_btn_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_size(obj,112,38);
    lv_obj_set_style_pad_all(obj,3,0);lv_obj_set_style_radius(obj,6,0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_bg_color(obj,lv_color_white(),0);
    lv_obj_set_style_text_color(obj,lv_color_hex(TEXT),0);
    lv_obj_set_style_border_width(obj,1,0);lv_obj_set_style_border_color(obj,lv_color_hex(BORDER),0);
    lv_obj_set_style_bg_color(obj,lv_palette_main(pressed),LV_STATE_PRESSED);
    lv_obj_set_style_text_color(obj,lv_color_white(),LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0xEDF1F5),LV_STATE_DISABLED);
    lv_obj_set_style_text_color(obj,lv_color_hex(0xA5AFBC),LV_STATE_DISABLED);
    lv_obj_t *name=lv_label_create(obj);lv_label_set_text(name,text);lv_obj_center(name);
    lv_obj_add_event_cb(obj,callback,LV_EVENT_CLICKED,NULL);return obj;
}
static void image(lv_obj_t *obj,uint8_t code,unsigned pixels)
{
    const lv_img_dsc_t *source=ui_weather_img_get(code);
    unsigned max=source->header.w>source->header.h?source->header.w:source->header.h;
    uint16_t zoom=max?(uint16_t)(pixels*256U/max):256;
    if(lv_img_get_src(obj)==source && lv_img_get_zoom(obj)==zoom)return;
    lv_img_set_src(obj,source);lv_img_set_size_mode(obj,LV_IMG_SIZE_MODE_REAL);
    lv_img_set_zoom(obj,zoom);
    lv_obj_set_size(obj,LV_SIZE_CONTENT,LV_SIZE_CONTENT);
    lv_obj_refresh_self_size(obj);
}
static int date_number(const char *date)
{
    unsigned y,m,d;
    if(sscanf(date,"%u-%u-%u",&y,&m,&d)!=3)return 0;
    /* 连续公历日序，处理月底、跨年和闰年。 */
    y-=m<=2;unsigned era=y/400,yoe=y-era*400;
    unsigned doy=(153U*(m>2?m-3:m+9)+2)/5+d-1;
    return (int)(era*146097U+yoe*365U+yoe/4U-yoe/100U+doy);
}
static void update(lv_timer_t *timer)
{
    (void)timer;
    const weather_snapshot_t *s=weather_service_get_snapshot();
    uint32_t second=lv_tick_get()/1000U;
    if(s->version==weather_ui.version && second==weather_ui.last_second)return;
    time_service_datetime_t now={0};char today[11]="";
    if(time_service_get_datetime(&now) && now.year>=1970 && now.year<=2099 && now.month>=1 && now.month<=12 && now.day>=1 && now.day<=31)
        snprintf(today,sizeof(today),"%04u-%02u-%02u",now.year,now.month,now.day);
    bool stale=(s->current_valid && (uint32_t)(lv_tick_get()-s->current_received_ms)>WEATHER_NOW_INTERVAL_MS) ||
               (s->forecast_valid && (uint32_t)(lv_tick_get()-s->forecast_received_ms)>WEATHER_DAILY_INTERVAL_MS);
    weather_ui.last_second=second;
    if(s->version==weather_ui.version && !strcmp(today,weather_ui.today) && stale==weather_ui.last_stale)return;
    weather_ui.version=s->version;weather_ui.last_stale=stale;
    strcpy(weather_ui.today,today);
    if(s->current_valid) {
        lv_label_set_text(weather_ui.city,s->current.city);
        lv_label_set_text_fmt(weather_ui.temperature,"当前 %d℃",s->current.temperature);
        lv_label_set_text(weather_ui.text,s->current.text);image(weather_ui.icon,s->current.code,64);
        char stamp[17];snprintf(stamp,sizeof(stamp),"%.16s",s->current.updated);
        for(unsigned i=0;stamp[i];i++)if(stamp[i]=='T')stamp[i]=' ';
        lv_label_set_text_fmt(weather_ui.updated,"数据更新 %s",stamp);
    }
    for(unsigned i=0;i<3;i++) {
        if(s->forecast_valid && i>=s->forecast.count) {
            lv_obj_add_flag(weather_ui.cards[i],LV_OBJ_FLAG_HIDDEN);continue;
        }
        lv_obj_clear_flag(weather_ui.cards[i],LV_OBJ_FLAG_HIDDEN);
        if(!s->forecast_valid || i>=s->forecast.count) {
            lv_label_set_text(weather_ui.dates[i],"预报待更新");
            lv_obj_add_flag(weather_ui.day_icons[i],LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(weather_ui.night_icons[i],LV_OBJ_FLAG_HIDDEN);continue;
        }
        const weather_daily_t *d=&s->forecast.days[i];
        int diff=today[0]?date_number(d->date)-date_number(today):99;
        const char *day=diff==0?"今天":diff==1?"明天":diff==2?"后天":"预报";
        lv_label_set_text_fmt(weather_ui.dates[i],"%s %.5s",day,d->date+5);
        lv_label_set_text_fmt(weather_ui.day_text[i],"日间 %s",d->text_day);
        lv_label_set_text_fmt(weather_ui.night_text[i],"夜间 %s",d->text_night);
        image(weather_ui.day_icons[i],d->code_day,32);image(weather_ui.night_icons[i],d->code_night,32);
        lv_obj_clear_flag(weather_ui.day_icons[i],LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(weather_ui.night_icons[i],LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(weather_ui.ranges[i],"%d ~ %d℃",d->low,d->high);
        if(d->wind_direction[0] && d->wind_scale[0])
            lv_label_set_text_fmt(weather_ui.winds[i],"%s %s级",d->wind_direction,d->wind_scale);
        else if(d->wind_direction[0] && d->wind_speed[0])
            lv_label_set_text_fmt(weather_ui.winds[i],"%s %s km/h",d->wind_direction,d->wind_speed);
        else lv_label_set_text(weather_ui.winds[i],"风况 —");
    }
    if(!s->connected)lv_label_set_text(weather_ui.status,s->current_valid||s->forecast_valid?
        "网络未连接，保留已有数据":"网络未连接");
    else if(s->current_status!=WEATHER_OK)
        lv_label_set_text_fmt(weather_ui.status,"实况：%s%s",weather_service_status_text(s->current_status),
            s->current_valid?"，保留旧数据":"");
    else if(s->forecast_status!=WEATHER_OK)
        lv_label_set_text_fmt(weather_ui.status,"预报：%s%s",weather_service_status_text(s->forecast_status),
            s->forecast_valid?"，保留旧数据":"");
    else lv_label_set_text(weather_ui.status,s->pending?"更新中，保留已有数据":stale?"数据已过期":
        s->current_valid&&s->forecast_valid?"已更新":"暂无天气数据");
    if(s->pending)lv_obj_add_state(weather_ui.refresh,LV_STATE_DISABLED);
    else lv_obj_clear_state(weather_ui.refresh,LV_STATE_DISABLED);
}
void ui_weather_create(lv_obj_t *parent)
{
    if(weather_ui.root){lv_obj_move_foreground(weather_ui.root);return;}
    weather_ui.root=panel(parent,false);lv_obj_set_size(weather_ui.root,lv_pct(100),lv_pct(92));
    lv_obj_set_y(weather_ui.root,lv_pct(8));lv_obj_set_style_pad_all(weather_ui.root,8,0);
    lv_obj_set_style_bg_opa(weather_ui.root,LV_OPA_COVER,0);lv_obj_set_style_bg_color(weather_ui.root,lv_color_hex(BG),0);
    lv_obj_set_style_text_font(weather_ui.root,&ui_font_chinese_24_4,0);
    lv_obj_set_style_text_color(weather_ui.root,lv_color_hex(TEXT),0);
    lv_obj_add_event_cb(weather_ui.root,root_deleted,LV_EVENT_DELETE,NULL);
    static const lv_coord_t cols[]={LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t rows[]={42,104,LV_GRID_FR(1),32,LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(weather_ui.root,cols,rows);
    lv_obj_t *header=panel(weather_ui.root,false);
    lv_obj_set_grid_cell(header,LV_GRID_ALIGN_STRETCH,0,1,LV_GRID_ALIGN_STRETCH,0,1);
    lv_obj_set_flex_flow(header,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_left(header,10,0);
    lv_obj_set_flex_align(header,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title=label(header,"天气",0xE91E63);
    lv_obj_set_style_text_color(title,lv_palette_main(LV_PALETTE_PINK),0);
    lv_obj_t *spacer=panel(header,false);lv_obj_set_size(spacer,0,0);lv_obj_set_flex_grow(spacer,1);
    weather_ui.refresh=button(header,"刷新",LV_PALETTE_GREEN,refresh_clicked);
    (void)button(header,"返回主页",LV_PALETTE_RED,close_clicked);
    lv_obj_t *current=panel(weather_ui.root,true);
    lv_obj_set_grid_cell(current,LV_GRID_ALIGN_STRETCH,0,1,LV_GRID_ALIGN_STRETCH,1,1);
    weather_ui.icon=lv_img_create(current);image(weather_ui.icon,99,64);lv_obj_align(weather_ui.icon,LV_ALIGN_LEFT_MID,4,0);
    /* 两组信息分别纵向均分，顶部、标签间和底部留白保持一致。 */
    lv_obj_t *current_left=panel(current,false);
    lv_obj_set_pos(current_left,86,0);lv_obj_set_size(current_left,150,lv_pct(100));
    lv_obj_set_flex_flow(current_left,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(current_left,LV_FLEX_ALIGN_SPACE_EVENLY,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER);
    weather_ui.city=label(current_left,WEATHER_CITY_NAME,CURRENT_ACCENT);lv_obj_set_width(weather_ui.city,150);
    weather_ui.temperature=label(current_left,"当前 —℃",CURRENT_ACCENT);lv_obj_set_width(weather_ui.temperature,150);

    lv_obj_t *current_right=panel(current,false);
    lv_obj_set_pos(current_right,260,0);lv_obj_set_size(current_right,490,lv_pct(100));
    lv_obj_set_flex_flow(current_right,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(current_right,LV_FLEX_ALIGN_SPACE_EVENLY,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER);
    weather_ui.text=label(current_right,"天气待更新",TEXT);lv_obj_set_width(weather_ui.text,490);
    weather_ui.updated=label(current_right,"数据更新 —",MUTED);lv_obj_set_width(weather_ui.updated,490);
    lv_obj_t *forecast=panel(weather_ui.root,false);
    lv_obj_set_grid_cell(forecast,LV_GRID_ALIGN_STRETCH,0,1,LV_GRID_ALIGN_STRETCH,2,1);
    static const lv_coord_t day_cols[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t day_rows[]={LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(forecast,day_cols,day_rows);
    for(unsigned i=0;i<3;i++) {
        lv_obj_t *card=weather_ui.cards[i]=panel(forecast,true);
        lv_obj_set_grid_cell(card,LV_GRID_ALIGN_STRETCH,i,1,LV_GRID_ALIGN_STRETCH,0,1);
        weather_ui.dates[i]=label(card,"预报待更新",forecast_accents[i]);lv_obj_set_width(weather_ui.dates[i],lv_pct(100));
        weather_ui.day_icons[i]=lv_img_create(card);image(weather_ui.day_icons[i],99,32);lv_obj_set_pos(weather_ui.day_icons[i],0,36);
        weather_ui.night_icons[i]=lv_img_create(card);image(weather_ui.night_icons[i],99,32);lv_obj_set_pos(weather_ui.night_icons[i],0,74);
        weather_ui.day_text[i]=label(card,"日间 —",TEXT);lv_obj_set_pos(weather_ui.day_text[i],40,38);lv_obj_set_width(weather_ui.day_text[i],188);
        weather_ui.night_text[i]=label(card,"夜间 —",NIGHT_TEXT);lv_obj_set_pos(weather_ui.night_text[i],40,76);lv_obj_set_width(weather_ui.night_text[i],188);
        weather_ui.ranges[i]=label(card,"— ~ —℃",TEMPERATURE_ACCENT);lv_obj_set_pos(weather_ui.ranges[i],0,114);lv_obj_set_width(weather_ui.ranges[i],lv_pct(100));
        weather_ui.winds[i]=label(card,"风况 —",MUTED);lv_obj_set_pos(weather_ui.winds[i],0,150);lv_obj_set_width(weather_ui.winds[i],lv_pct(100));
    }
    lv_obj_t *footer=panel(weather_ui.root,false);
    lv_obj_set_grid_cell(footer,LV_GRID_ALIGN_STRETCH,0,1,LV_GRID_ALIGN_STRETCH,3,1);
    lv_obj_set_flex_flow(footer,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    label(footer,"数据来源：心知天气",MUTED);
    weather_ui.status=label(footer,"暂无天气数据",MUTED);lv_obj_set_width(weather_ui.status,0);lv_obj_set_flex_grow(weather_ui.status,1);
    lv_obj_set_style_text_align(weather_ui.status,LV_TEXT_ALIGN_RIGHT,0);
    weather_ui.version=UINT32_MAX;weather_ui.timer=lv_timer_create(update,200,NULL);update(NULL);
    weather_service_poll(lv_tick_get());
}
