#include "screen_mgr.h"

#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "screen";

static const screen_t *s_current;

// 轉場長度。再長會讓「點一下就走」的操作感變鈍，
// 而這片面板畫一張全螢幕就要 32ms，拉長也只是多畫幾張
#define TRANSITION_MS 150

void screen_load(lv_obj_t *scr)
{
    // auto_del = false：畫面物件建一次就快取著，不能讓 LVGL 幫忙刪掉舊的
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, TRANSITION_MS, 0, false);
}

esp_err_t screen_mgr_init(const screen_t *initial)
{
    ESP_RETURN_ON_FALSE(initial && initial->enter, ESP_ERR_INVALID_ARG, TAG, "畫面要有 enter()");
    s_current = NULL;
    return screen_mgr_goto(initial);
}

esp_err_t screen_mgr_goto(const screen_t *next)
{
    ESP_RETURN_ON_FALSE(next && next->enter, ESP_ERR_INVALID_ARG, TAG, "畫面要有 enter()");
    if (next == s_current) return ESP_OK;

    if (s_current && s_current->exit) s_current->exit();

    esp_err_t err = next->enter();
    if (err != ESP_OK) {
        // 進不去就留在原畫面，總比兩邊都沒有好
        ESP_LOGE(TAG, "進入「%s」失敗（%s），維持在「%s」",
                 next->name, esp_err_to_name(err), s_current ? s_current->name : "(無)");
        return err;
    }

    ESP_LOGI(TAG, "%s → %s", s_current ? s_current->name : "(開機)", next->name);
    s_current = next;
    return ESP_OK;
}

void screen_mgr_tick(void)
{
    if (s_current && s_current->tick) s_current->tick();
}

bool screen_mgr_dispatch(screen_event_t ev)
{
    if (s_current && s_current->on_event) return s_current->on_event(ev);
    return false;
}

const screen_t *screen_mgr_current(void) { return s_current; }
