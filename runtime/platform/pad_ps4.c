#include "media.h"
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <string.h>
static SpMutex *lock;
static int handles[4] = {-1,-1,-1,-1}, users[4] = {-1,-1,-1,-1}, hidden[4], connected[4];
static int assignments[4] = {0,1,2,3};
static NnPadState states[4];
static uint64_t last_poll, last_users;
int nnpad_init(void) {
    if (lock) return 1;
    int rc = sceUserServiceInitialize(NULL);
    if (rc && (uint32_t)rc != ORBIS_USER_SERVICE_ERROR_ALREADY_INITIALIZED) return 0;
    if (scePadInit() || sp_mutex_create(&lock)) return 0;
    return 1;
}
void nnpad_poll(int force) {
    if (!lock) return;
    uint64_t now = sp_now_us();
    if (sp_mutex_lock(lock)) return;
    if (!force && now - last_poll < 1000) { sp_mutex_unlock(lock); return; }
    last_poll = now;
    if (!last_users || now - last_users >= 1000000) {
        OrbisUserServiceLoginUserIdList list;
        memset(&list, 0xff, sizeof list);
        if (sceUserServiceGetLoginUserIdList(&list)) sceUserServiceGetInitialUser(&list.userId[0]);
        for (unsigned i = 0; i < 4; ++i) {
            if (users[i] != list.userId[i]) {
                if (handles[i] >= 0) scePadClose(handles[i]);
                users[i] = list.userId[i]; handles[i] = -1; hidden[i] = 0;
            }
            if (handles[i] < 0 && users[i] >= 0) handles[i] = scePadOpen(users[i], 0, 0, NULL);
        }
        last_users = now;
    }
    for (unsigned i = 0; i < 4; ++i) {
        OrbisPadData data = {0};
        connected[i] = handles[i] >= 0 && !scePadReadState(handles[i], &data) && data.connected;
        SpDs4Sample raw = {data.buttons, data.leftStick.x, data.leftStick.y,
                          data.rightStick.x, data.rightStick.y, data.analogButtons.l2,
                          data.analogButtons.r2, (uint8_t)connected[i]};
        sp_ds4_decode(&raw, &states[i]);
        if (hidden[i] && states[i].buttons) hidden[i] = 0;
    }
    sp_mutex_unlock(lock);
}
int nnpad_get(int player, NnPadState *out) {
    if (!out || !lock || player < 0 || player >= 4) return 0;
    sp_mutex_lock(lock);
    player = assignments[player];
    int present = connected[player] && !hidden[player];
    if (present) *out = states[player]; else memset(out, 0, sizeof *out);
    sp_mutex_unlock(lock); return present;
}
int nnpad_present(int player) { NnPadState state; return nnpad_get(player, &state); }
void nnpad_rumble(int player, float low, float high) {
    if (!lock || player < 0 || player >= 4) return;
    if (!(low >= 0)) low = 0; if (low > 1) low = 1;
    if (!(high >= 0)) high = 0; if (high > 1) high = 1;
    OrbisPadVibeParam vibration = {0};
    vibration.lgMotor = (uint8_t)(low * 255); vibration.smMotor = (uint8_t)(high * 255);
    sp_mutex_lock(lock);
    player = assignments[player];
    if (handles[player] >= 0) scePadSetVibration(handles[player], &vibration);
    sp_mutex_unlock(lock);
}
void nnpad_disconnect(int player) {
    if (!lock || player < 0 || player >= 4) return;
    sp_mutex_lock(lock); hidden[assignments[player]] = 1; sp_mutex_unlock(lock);
}
void nnpad_swap(int a, int b) {
    if (!lock || a < 0 || b < 0 || a >= 4 || b >= 4) return;
    sp_mutex_lock(lock);
    int temporary = assignments[a]; assignments[a] = assignments[b]; assignments[b] = temporary;
    sp_mutex_unlock(lock);
}
int nnpad_keyboard_player(void) { return -1; }
int nnpad_kb_vk(int index) { (void)index; return 0; }
void sp_pad_shutdown(void) {
    if (!lock) return;
    for (int i = 0; i < 4; ++i) {
        nnpad_rumble(i, 0, 0);
        if (handles[i] >= 0) scePadClose(handles[i]);
        handles[i] = users[i] = -1;
    }
    memset(states, 0, sizeof states); memset(connected, 0, sizeof connected);
    sp_mutex_destroy(&lock); last_users = last_poll = 0;
}
