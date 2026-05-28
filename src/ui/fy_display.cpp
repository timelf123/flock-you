#include "fy_display.h"

#ifndef FY_HAS_DISPLAY

void fyDisplayInit() {}
void fyUiTick() {}
void fyUiNotifyNewDetection() {}

#else

#include "fy_app.h"
#include "fy_audio.h"
#include "fy_board.h"
#include "fy_display_hw.h"
#include "fy_shared.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

static const lv_color_t COL_BG = lv_color_hex(0x0a0012);
static const lv_color_t COL_PINK = lv_color_hex(0xec4899);
static const lv_color_t COL_PURPLE = lv_color_hex(0x8b5cf6);
static const lv_color_t COL_LAVENDER = lv_color_hex(0xc084fc);
static const lv_color_t COL_RAVEN = lv_color_hex(0xef4444);
static const lv_color_t COL_GPS = lv_color_hex(0x22c55e);
static const lv_color_t COL_MUTED = lv_color_hex(0x666666);

static lv_obj_t* s_root = nullptr;
static lv_obj_t* s_statDet = nullptr;
static lv_obj_t* s_statRaven = nullptr;
static lv_obj_t* s_statBle = nullptr;
static lv_obj_t* s_statGps = nullptr;
static lv_obj_t* s_panels[4] = {nullptr};
static lv_obj_t* s_tabBtns[4] = {nullptr};
static lv_obj_t* s_liveList = nullptr;
static lv_obj_t* s_prevList = nullptr;
static lv_obj_t* s_dbList = nullptr;
static lv_obj_t* s_toolsPanel = nullptr;
static lv_obj_t* s_detailModal = nullptr;

static int s_activeTab = 0;
static unsigned long s_lastRefresh = 0;
static bool s_prevLoaded = false;
static FYDetection s_prevBuf[FY_MAX_DETECTIONS];
static int s_prevCount = 0;
static int s_dbCategory = -1;
static FYDetection s_sortBuf[FY_MAX_DETECTIONS];
static FYDetection s_cardData[FY_MAX_DETECTIONS];

static void dbShowCategories();
static void dbCategoryClicked(lv_event_t* e);

static lv_color_t rssiColor(int rssi) {
    if (rssi >= -60) return COL_GPS;
    if (rssi >= -80) return lv_color_hex(0xfacc15);
    return COL_RAVEN;
}

static void styleCard(lv_obj_t* card) {
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2d1b69), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_60, 0);
    lv_obj_set_style_border_color(card, COL_PURPLE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
}

static lv_obj_t* addDetectionCard(lv_obj_t* parent, const FYDetection& d) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    styleCard(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);

    lv_obj_t* row1 = lv_obj_create(card);
    lv_obj_remove_style_all(row1);
    lv_obj_set_width(row1, lv_pct(100));
    lv_obj_set_height(row1, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row1, 4, 0);

    lv_obj_t* macLbl = lv_label_create(row1);
    lv_label_set_text(macLbl, d.mac);
    lv_obj_set_style_text_color(macLbl, COL_PINK, 0);
    lv_obj_set_style_text_font(macLbl, &lv_font_montserrat_14, 0);

    if (d.name[0]) {
        lv_obj_t* nm = lv_label_create(row1);
        lv_label_set_text(nm, d.name);
        lv_obj_set_style_text_color(nm, COL_LAVENDER, 0);
    }

    lv_obj_t* chips = lv_obj_create(card);
    lv_obj_remove_style_all(chips);
    lv_obj_set_width(chips, lv_pct(100));
    lv_obj_set_height(chips, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(chips, 4, 0);
    lv_obj_set_style_pad_row(chips, 4, 0);

    char buf[48];
    snprintf(buf, sizeof(buf), "RSSI %d", d.rssi);
    lv_obj_t* rssi = lv_label_create(chips);
    lv_label_set_text(rssi, buf);
    lv_obj_set_style_text_color(rssi, rssiColor(d.rssi), 0);

    lv_obj_t* meth = lv_label_create(chips);
    lv_label_set_text(meth, d.method);
    lv_obj_set_style_text_color(meth, COL_PURPLE, 0);

    snprintf(buf, sizeof(buf), "x%d", d.count);
    lv_obj_t* cnt = lv_label_create(chips);
    lv_label_set_text(cnt, buf);
    lv_obj_set_style_text_color(cnt, COL_PINK, 0);

    if (d.isRaven) {
        snprintf(buf, sizeof(buf), "RAVEN %s", d.ravenFW[0] ? d.ravenFW : "?");
        lv_obj_t* rv = lv_label_create(chips);
        lv_label_set_text(rv, buf);
        lv_obj_set_style_text_color(rv, COL_RAVEN, 0);
    }

    if (d.hasGPS) {
        snprintf(buf, sizeof(buf), "%.5f,%.5f", d.gpsLat, d.gpsLon);
        lv_obj_t* gps = lv_label_create(chips);
        lv_label_set_text(gps, buf);
        lv_obj_set_style_text_color(gps, COL_GPS, 0);
    } else {
        lv_obj_t* gps = lv_label_create(chips);
        lv_label_set_text(gps, "no gps");
        lv_obj_set_style_text_color(gps, COL_MUTED, 0);
    }

    return card;
}

static void showDetail(const FYDetection& d) {
    if (s_detailModal) {
        lv_obj_delete(s_detailModal);
        s_detailModal = nullptr;
    }

    char body[320];
    snprintf(body, sizeof(body),
             "MAC: %s\nName: %s\nRSSI: %d\nMethod: %s\nCount: %d\n"
             "Raven: %s %s\nFirst: %lu ms\nLast: %lu ms\nGPS: %s",
             d.mac, d.name[0] ? d.name : "-", d.rssi, d.method, d.count,
             d.isRaven ? "yes" : "no", d.ravenFW,
             d.firstSeen, d.lastSeen,
             d.hasGPS ? "tagged" : "none");

    s_detailModal = lv_msgbox_create(s_root);
    lv_msgbox_add_title(s_detailModal, d.mac);
    lv_msgbox_add_text(s_detailModal, body);
    lv_msgbox_add_close_button(s_detailModal);
    lv_obj_center(s_detailModal);
}

static void liveCardClicked(lv_event_t* e) {
    FYDetection* d = (FYDetection*)lv_event_get_user_data(e);
    if (d) showDetail(*d);
}

static void rebuildLiveList() {
    if (!s_liveList) return;
    lv_obj_clean(s_liveList);

    int count = fyAppGetDetectionCount();
    if (count <= 0) {
        lv_obj_t* empty = lv_label_create(s_liveList);
        lv_label_set_text(empty, "Scanning...\nBLE active on all channels");
        lv_obj_set_style_text_color(empty, COL_PURPLE, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty);
        return;
    }

    if (count > FY_MAX_DETECTIONS) count = FY_MAX_DETECTIONS;
    for (int i = 0; i < count; i++) {
        if (!fyAppCopyDetection(i, &s_sortBuf[i])) {
            count = i;
            break;
        }
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (s_sortBuf[j].lastSeen > s_sortBuf[i].lastSeen) {
                FYDetection t = s_sortBuf[i];
                s_sortBuf[i] = s_sortBuf[j];
                s_sortBuf[j] = t;
            }
        }
    }

    for (int i = 0; i < count; i++) {
        s_cardData[i] = s_sortBuf[i];
        lv_obj_t* card = addDetectionCard(s_liveList, s_sortBuf[i]);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, liveCardClicked, LV_EVENT_CLICKED, &s_cardData[i]);
    }
}

static void rebuildPrevList() {
    if (!s_prevList) return;
    lv_obj_clean(s_prevList);

    if (!s_prevLoaded) {
        s_prevCount = 0;
        fyAppLoadPrevSession(s_prevBuf, FY_MAX_DETECTIONS, &s_prevCount);
        s_prevLoaded = true;
    }

    if (s_prevCount <= 0) {
        lv_obj_t* empty = lv_label_create(s_prevList);
        lv_label_set_text(empty, "No prior session data");
        lv_obj_set_style_text_color(empty, COL_PURPLE, 0);
        lv_obj_center(empty);
        return;
    }

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "%d detections (prior session)", s_prevCount);
    lv_obj_t* h = lv_label_create(s_prevList);
    lv_label_set_text(h, hdr);
    lv_obj_set_style_text_color(h, COL_PURPLE, 0);

    for (int i = 0; i < s_prevCount; i++) {
        addDetectionCard(s_prevList, s_prevBuf[i]);
    }
}

static void dbShowCategories() {
    s_dbCategory = -1;
    lv_obj_clean(s_dbList);

    size_t cFlock = 0, cMfr = 0, cSt = 0, cNames = 0, cMfrId = 0, cRaven = 0;
    fyAppGetPatternCounts(&cFlock, &cMfr, &cSt, &cNames, &cMfrId, &cRaven);

    struct { const char* title; int cat; } items[] = {
        {"Flock MAC prefixes", 0},
        {"Contract mfr MACs", 1},
        {"SoundThinking MACs", 2},
        {"BLE device names", 3},
        {"Manufacturer IDs", 4},
        {"Raven UUIDs", 5},
    };

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        lv_obj_t* btn = lv_button_create(s_dbList);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_style_bg_color(btn, COL_PURPLE, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, items[i].title);
        lv_obj_add_event_cb(btn, dbCategoryClicked, LV_EVENT_CLICKED, (void*)(intptr_t)items[i].cat);
    }
}

static void dbCategoryClicked(lv_event_t* e) {
    int cat = (int)(intptr_t)lv_event_get_user_data(e);
    s_dbCategory = cat;
    lv_obj_clean(s_dbList);

    lv_obj_t* back = lv_button_create(s_dbList);
    lv_obj_set_width(back, lv_pct(100));
    lv_obj_set_style_bg_color(back, COL_PINK, 0);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, "< Back");
    lv_obj_add_event_cb(back, [](lv_event_t* ev) {
        (void)ev;
        dbShowCategories();
    }, LV_EVENT_CLICKED, nullptr);

    size_t count = 0;
    const char* const* list = nullptr;

    switch (cat) {
        case 0: list = fyAppGetFlockMacPrefixes(&count); break;
        case 1: list = fyAppGetMfrMacPrefixes(&count); break;
        case 2: list = fyAppGetSoundThinkingMacPrefixes(&count); break;
        case 3: list = fyAppGetDeviceNamePatterns(&count); break;
        case 5: list = fyAppGetRavenServiceUuids(&count); break;
        default: break;
    }

    if (cat == 4) {
        size_t n = 0;
        const uint16_t* ids = fyAppGetBleManufacturerIds(&n);
        for (size_t i = 0; i < n; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%04X", ids[i]);
            lv_obj_t* chip = lv_label_create(s_dbList);
            lv_label_set_text(chip, buf);
            lv_obj_set_style_text_color(chip, COL_LAVENDER, 0);
        }
        return;
    }

    for (size_t i = 0; i < count; i++) {
        lv_obj_t* chip = lv_label_create(s_dbList);
        lv_label_set_text(chip, list[i]);
        lv_obj_set_style_text_color(chip, COL_LAVENDER, 0);
        if (cat == 5) {
            lv_obj_set_style_text_font(chip, &lv_font_montserrat_12, 0);
        }
    }
}

static void buildDbTab() {
    s_dbList = lv_obj_create(s_panels[2]);
    lv_obj_remove_style_all(s_dbList);
    lv_obj_set_size(s_dbList, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_dbList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_dbList, 4, 0);
    lv_obj_set_style_pad_row(s_dbList, 6, 0);
    lv_obj_set_scrollbar_mode(s_dbList, LV_SCROLLBAR_MODE_AUTO);
    dbShowCategories();
}

static void muteClicked(lv_event_t* e) {
    (void)e;
    bool m = !fyAudioIsMuted();
    fyAudioSetMuted(m);
    lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
    lv_label_set_text(lbl, m ? "UNMUTE ALERTS" : "MUTE ALERTS");
}

static void clearClicked(lv_event_t* e) {
    (void)e;
    fySaveSession();
    fyAppClearDetections();
    s_prevLoaded = false;
    rebuildLiveList();
}

static void buildToolsTab() {
    s_toolsPanel = lv_obj_create(s_panels[3]);
    lv_obj_remove_style_all(s_toolsPanel);
    lv_obj_set_size(s_toolsPanel, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_toolsPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_toolsPanel, 8, 0);
    lv_obj_set_style_pad_row(s_toolsPanel, 8, 0);

    lv_obj_t* h = lv_label_create(s_toolsPanel);
    lv_label_set_text(h, "EXPORT");
    lv_obj_set_style_text_color(h, COL_PINK, 0);

    lv_obj_t* note = lv_label_create(s_toolsPanel);
    lv_label_set_text(note,
                      "Join WiFi flockyou / flockyou123\n"
                      "Open http://192.168.4.1\n"
                      "Use TOOLS tab in browser for JSON/CSV/KML");
    lv_obj_set_style_text_color(note, COL_PURPLE, 0);
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);

    char ipLine[64];
    snprintf(ipLine, sizeof(ipLine), "AP IP: %s", WiFi.softAPIP().toString().c_str());
    lv_obj_t* ip = lv_label_create(s_toolsPanel);
    lv_label_set_text(ip, ipLine);
    lv_obj_set_style_text_color(ip, COL_GPS, 0);

    lv_obj_t* muteBtn = lv_button_create(s_toolsPanel);
    lv_obj_set_width(muteBtn, lv_pct(100));
    lv_obj_set_style_bg_color(muteBtn, COL_PURPLE, 0);
    lv_obj_t* muteLbl = lv_label_create(muteBtn);
    lv_label_set_text(muteLbl, fyAudioIsMuted() ? "UNMUTE ALERTS" : "MUTE ALERTS");
    lv_obj_add_event_cb(muteBtn, muteClicked, LV_EVENT_CLICKED, muteLbl);

    lv_obj_t* clrBtn = lv_button_create(s_toolsPanel);
    lv_obj_set_width(clrBtn, lv_pct(100));
    lv_obj_set_style_bg_color(clrBtn, COL_RAVEN, 0);
    lv_obj_t* clrLbl = lv_label_create(clrBtn);
    lv_label_set_text(clrLbl, "CLEAR ALL DETECTIONS");
    lv_obj_add_event_cb(clrBtn, clearClicked, LV_EVENT_CLICKED, nullptr);
}

static void setActiveTab(int idx) {
    s_activeTab = idx;
    for (int i = 0; i < 4; i++) {
        if (s_panels[i]) {
            if (i == idx) lv_obj_remove_flag(s_panels[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(s_panels[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_tabBtns[i]) {
            if (i == idx) {
                lv_obj_set_style_text_color(s_tabBtns[i], COL_PINK, 0);
                lv_obj_set_style_border_side(s_tabBtns[i], LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_color(s_tabBtns[i], COL_PINK, 0);
                lv_obj_set_style_border_width(s_tabBtns[i], 2, 0);
            } else {
                lv_obj_set_style_text_color(s_tabBtns[i], COL_PURPLE, 0);
                lv_obj_set_style_border_width(s_tabBtns[i], 0, 0);
            }
        }
    }
    if (idx == 1) rebuildPrevList();
    if (idx == 0) rebuildLiveList();
}

static void tabClicked(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    setActiveTab(idx);
}

static void gpsStatClicked(lv_event_t* e) {
    (void)e;
    int tagged = 0, total = 0;
    bool valid = false;
    unsigned long age = 0;
    fyAppGetGpsStats(&tagged, &total, &valid, &age);

    char msg[96];
    snprintf(msg, sizeof(msg), "GPS %s\nTagged %d/%d\nAge %lu ms",
             valid ? "OK" : "OFF", tagged, total, age);

    lv_obj_t* mbox = lv_msgbox_create(s_root);
    lv_msgbox_add_title(mbox, "GPS");
    lv_msgbox_add_text(mbox, msg);
    lv_msgbox_add_close_button(mbox);
    lv_obj_center(mbox);
}

static void buildUi() {
    s_root = lv_screen_active();
    lv_obj_set_style_bg_color(s_root, COL_BG, 0);

    lv_obj_t* header = lv_obj_create(s_root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), 36);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a0033), 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, COL_PINK, 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_pad_left(header, 8, 0);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "FLOCK-YOU");
    lv_obj_set_style_text_color(title, COL_PINK, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, -6);

    lv_obj_t* sub = lv_label_create(header);
    lv_label_set_text(sub, "Surveillance Detector");
    lv_obj_set_style_text_color(sub, COL_PURPLE, 0);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 0, 10);

    lv_obj_t* stats = lv_obj_create(s_root);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, lv_pct(100), 40);
    lv_obj_align(stats, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(stats, 4, 0);
    lv_obj_set_style_pad_column(stats, 4, 0);

    const char* labels[] = {"DET", "RAVEN", "BLE", "GPS"};
    lv_obj_t** vals[] = {&s_statDet, &s_statRaven, &s_statBle, &s_statGps};

    for (int i = 0; i < 4; i++) {
        lv_obj_t* box = lv_obj_create(stats);
        lv_obj_set_flex_grow(box, 1);
        lv_obj_set_height(box, lv_pct(100));
        lv_obj_set_style_bg_color(box, lv_color_hex(0x1a1033), 0);
        lv_obj_set_style_border_color(box, COL_PURPLE, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_radius(box, 4, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        *vals[i] = lv_label_create(box);
        lv_label_set_text(*vals[i], "0");
        lv_obj_set_style_text_color(*vals[i], COL_PINK, 0);

        lv_obj_t* cap = lv_label_create(box);
        lv_label_set_text(cap, labels[i]);
        lv_obj_set_style_text_color(cap, COL_PURPLE, 0);
        if (i == 3) {
            lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(box, gpsStatClicked, LV_EVENT_CLICKED, nullptr);
        }
    }

    lv_obj_t* tabbar = lv_obj_create(s_root);
    lv_obj_remove_style_all(tabbar);
    lv_obj_set_size(tabbar, lv_pct(100), 32);
    lv_obj_align(tabbar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(tabbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_border_side(tabbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(tabbar, COL_PURPLE, 0);
    lv_obj_set_style_border_width(tabbar, 1, 0);

    const char* tabs[] = {"LIVE", "PREV", "DB", "TOOLS"};
    for (int i = 0; i < 4; i++) {
        s_tabBtns[i] = lv_button_create(tabbar);
        lv_obj_remove_style_all(s_tabBtns[i]);
        lv_obj_set_flex_grow(s_tabBtns[i], 1);
        lv_obj_set_height(s_tabBtns[i], lv_pct(100));
        lv_obj_set_style_bg_opa(s_tabBtns[i], LV_OPA_TRANSP, 0);
        lv_obj_t* tl = lv_label_create(s_tabBtns[i]);
        lv_label_set_text(tl, tabs[i]);
        lv_obj_center(tl);
        lv_obj_add_event_cb(s_tabBtns[i], tabClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    lv_obj_move_foreground(tabbar);

    lv_obj_t* content = lv_obj_create(s_root);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_height(content, FY_SCREEN_H - 36 - 40 - 32);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 76);

    for (int i = 0; i < 4; i++) {
        s_panels[i] = lv_obj_create(content);
        lv_obj_remove_style_all(s_panels[i]);
        lv_obj_set_size(s_panels[i], lv_pct(100), lv_pct(100));
        lv_obj_add_flag(s_panels[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_liveList = lv_obj_create(s_panels[0]);
    lv_obj_remove_style_all(s_liveList);
    lv_obj_set_size(s_liveList, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_liveList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_liveList, 6, 0);
    lv_obj_set_scrollbar_mode(s_liveList, LV_SCROLLBAR_MODE_AUTO);

    s_prevList = lv_obj_create(s_panels[1]);
    lv_obj_remove_style_all(s_prevList);
    lv_obj_set_size(s_prevList, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_prevList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_prevList, 6, 0);
    lv_obj_set_scrollbar_mode(s_prevList, LV_SCROLLBAR_MODE_AUTO);

    buildDbTab();
    buildToolsTab();

    setActiveTab(0);
}

static void updateStats() {
    if (!s_statDet) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", fyAppGetDetectionCount());
    lv_label_set_text(s_statDet, buf);
    snprintf(buf, sizeof(buf), "%d", fyAppGetRavenCount());
    lv_label_set_text(s_statRaven, buf);
    lv_label_set_text(s_statBle, fyAppIsBleActive() ? "ON" : "OFF");

    int tagged = 0, total = 0;
    bool valid = false;
    unsigned long age = 0;
    fyAppGetGpsStats(&tagged, &total, &valid, &age);
    if (valid) snprintf(buf, sizeof(buf), "%d/%d", tagged, total);
    else snprintf(buf, sizeof(buf), "OFF");
    lv_label_set_text(s_statGps, buf);
    lv_obj_set_style_text_color(s_statGps, valid ? COL_GPS : COL_RAVEN, 0);
}

void fyDisplayInit() {
    if (!fyDisplayHwInit()) {
        printf("[FLOCK-YOU] Display init failed\n");
        return;
    }
    buildUi();
    updateStats();
    rebuildLiveList();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    printf("[FLOCK-YOU] LCD UI ready\n");
}

void fyUiTick() {
    fyDisplayHwTick();
    if (millis() - s_lastRefresh >= 2500) {
        s_lastRefresh = millis();
        updateStats();
        if (s_activeTab == 0) rebuildLiveList();
    }
}

void fyUiNotifyNewDetection() {
    updateStats();
    if (s_activeTab == 0) rebuildLiveList();
}

#endif
