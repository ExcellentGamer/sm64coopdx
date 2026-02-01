#include <stdio.h>
#include <string.h>

#include "network.h"
#include "behavior_table.h"
#include "game/camera.h"
#include "game/characters.h"
#include "game/envfx_snow.h"
#include "game/first_person_cam.h"
#include "game/hardcoded.h"
#include "game/level_update.h"
#include "game/level_geo.h"
#include "game/mario.h"
#include "game/mario_misc.h"
#include "game/object_helpers.h"
#include "game/save_file.h"
#include "game/scroll_targets.h"
#include "game/skybox.h"
#include "game/rendering_graph_node.h"
#include "engine/lighting_engine.h"
#include "engine/math_util.h"
#include "pc/configfile.h"
#include "pc/djui/djui.h"
#include "pc/djui/djui_hud_utils.h"
#include "pc/djui/djui_panel.h"
#include "pc/djui/djui_panel_main.h"
#include "pc/lua/smlua.h"
#include "pc/lua/utils/smlua_camera_utils.h"
#include "pc/lua/utils/smlua_misc_utils.h"
#include "pc/mods/mods.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/fs/fmem.h"
#include "pc/pc_main.h"
#include "pc/utils/misc.h"

u16 networkLoadingLevel = 0;

char gGetHostName[MAX_CONFIG_STRING] = "";

enum NetworkType gNetworkType = NT_NONE;

static bool sNetworkRestarting = false;

static inline void color_set(Color color, u8 r, u8 g, u8 b) {
    color[0] = r;
    color[1] = g;
    color[2] = b;
}

static bool ns_stub_initialize(UNUSED enum NetworkType networkType, UNUSED bool reconnecting) { return true; }
static s64 ns_stub_get_id(UNUSED u8 localIndex) { return 0; }
static char* ns_stub_get_id_str(UNUSED u8 localIndex) { return "0"; }
static void ns_stub_save_id(UNUSED u8 localIndex, UNUSED s64 networkId) { }
static void ns_stub_clear_id(UNUSED u8 localIndex) { }
static void* ns_stub_dup_addr(UNUSED u8 localIndex) { return NULL; }
static bool ns_stub_match_addr(UNUSED void* addr1, UNUSED void* addr2) { return true; }
static void ns_stub_update(void) { }
static int ns_stub_send(UNUSED u8 localIndex, UNUSED void* addr, UNUSED u8* data, UNUSED u16 dataLength) { return 0; }
static void ns_stub_get_lobby_id(UNUSED char* destination, UNUSED u32 destLength) { }
static void ns_stub_get_lobby_secret(UNUSED char* destination, UNUSED u32 destLength) { }
static void ns_stub_shutdown(UNUSED bool reconnecting) { }

struct NetworkSystem gNetworkSystemSocket = {
    .initialize = ns_stub_initialize,
    .get_id = ns_stub_get_id,
    .get_id_str = ns_stub_get_id_str,
    .save_id = ns_stub_save_id,
    .clear_id = ns_stub_clear_id,
    .dup_addr = ns_stub_dup_addr,
    .match_addr = ns_stub_match_addr,
    .update = ns_stub_update,
    .send = ns_stub_send,
    .get_lobby_id = ns_stub_get_lobby_id,
    .get_lobby_secret = ns_stub_get_lobby_secret,
    .shutdown = ns_stub_shutdown,
    .requireServerBroadcast = false,
    .name = "Stub",
};

struct NetworkSystem gNetworkSystemCoopNet = {
    .initialize = ns_stub_initialize,
    .get_id = ns_stub_get_id,
    .get_id_str = ns_stub_get_id_str,
    .save_id = ns_stub_save_id,
    .clear_id = ns_stub_clear_id,
    .dup_addr = ns_stub_dup_addr,
    .match_addr = ns_stub_match_addr,
    .update = ns_stub_update,
    .send = ns_stub_send,
    .get_lobby_id = ns_stub_get_lobby_id,
    .get_lobby_secret = ns_stub_get_lobby_secret,
    .shutdown = ns_stub_shutdown,
    .requireServerBroadcast = false,
    .name = "Stub",
};

struct NetworkSystem* gNetworkSystem = &gNetworkSystemSocket;

bool gNetworkAreaLoaded = true;
bool gNetworkAreaSyncing = false;
u32 gNetworkAreaTimer = 0;
u32 gNetworkAreaTimerClock = 0;
void* gNetworkServerAddr = NULL;

struct ServerSettings gServerSettings = {
    .playerInteractions = PLAYER_INTERACTIONS_SOLID,
    .bouncyLevelBounds = BOUNCY_LEVEL_BOUNDS_OFF,
    .pvpType = PLAYER_PVP_CLASSIC,
    .playerKnockbackStrength = 25,
    .stayInLevelAfterStar = FALSE,
    .skipIntro = FALSE,
    .bubbleDeath = TRUE,
    .enablePlayersInLevelDisplay = TRUE,
    .enablePlayerList = TRUE,
    .headlessServer = FALSE,
    .nametags = TRUE,
    .maxPlayers = MAX_PLAYERS,
    .pauseAnywhere = FALSE,
};

struct NametagsSettings gNametagsSettings = {
    .showHealth = false,
    .showSelfTag = false,
};

bool gNetworkSentJoin = false;
u16 gNetworkRequestLocationTimer = 0;

u8 gDebugPacketIdBuffer[256] = { 0xFF };
u8 gDebugPacketSentBuffer[256] = { 0 };
u8 gDebugPacketOnBuffer = 0;

u32 gNetworkStartupTimer = 0;

struct NetworkPlayer gNetworkPlayers[MAX_PLAYERS] = { 0 };
struct NetworkPlayer* gNetworkPlayerLocal = &gNetworkPlayers[0];
struct NetworkPlayer* gNetworkPlayerServer = &gNetworkPlayers[0];

static void network_apply_config_to_server_settings(void) {
    gServerSettings.playerInteractions = configPlayerInteraction;
    gServerSettings.bouncyLevelBounds = configBouncyLevelBounds;
    gServerSettings.playerKnockbackStrength = configPlayerKnockbackStrength;
    gServerSettings.stayInLevelAfterStar = configStayInLevelAfterStar;
    gServerSettings.skipIntro = gCLIOpts.skipIntro ? TRUE : configSkipIntro;
    gServerSettings.bubbleDeath = configBubbleDeath;
    gServerSettings.enablePlayersInLevelDisplay = TRUE;
    gServerSettings.enablePlayerList = TRUE;
    gServerSettings.headlessServer = FALSE;
    gServerSettings.nametags = configNametags;
    gServerSettings.maxPlayers = 1;
    gServerSettings.pauseAnywhere = configPauseAnywhere;
    gServerSettings.pvpType = configPvpType;
}

void network_set_system(UNUSED enum NetworkSystemType nsType) {
}

bool network_init(UNUSED enum NetworkType inNetworkType, UNUSED bool reconnecting) {
    gNetworkType = NT_NONE;

    gNetworkAreaLoaded = true;
    gNetworkAreaSyncing = false;
    gNetworkAreaTimer = 0;
    gNetworkAreaTimerClock = 0;
    gNetworkSentJoin = false;
    gNetworkRequestLocationTimer = 0;
    gNetworkServerAddr = NULL;

    network_apply_config_to_server_settings();

    gNetworkPlayers[0].connected = true;
    gNetworkPlayers[0].type = NPT_LOCAL;
    gNetworkPlayers[0].localIndex = 0;
    gNetworkPlayers[0].globalIndex = 0;
    gNetworkPlayers[0].moderator = false;
    gNetworkPlayers[0].palette = configPlayerPalette;
    gNetworkPlayers[0].overridePalette = gNetworkPlayers[0].palette;
    gNetworkPlayers[0].modelIndex = (configPlayerModel < CT_MAX) ? configPlayerModel : CT_MARIO;
    gNetworkPlayers[0].overrideModelIndex = gNetworkPlayers[0].modelIndex;
    snprintf(gNetworkPlayers[0].name, MAX_CONFIG_STRING, "%s", "Player");
    snprintf(gNetworkPlayers[0].discordId, 64, "%s", "0");

    network_player_update_model(0);

    return true;
}

void network_on_init_area(void) {
}

void network_on_loaded_area(void) {
}

bool network_allow_unknown_local_index(UNUSED enum PacketType packetType) {
    return false;
}

void network_send_to(UNUSED u8 localIndex, UNUSED struct Packet* p) {
}

void network_send(UNUSED struct Packet* p) {
}

void network_receive(UNUSED u8 localIndex, UNUSED void* addr, UNUSED u8* data, UNUSED u16 dataLength) {
}

void* network_duplicate_address(UNUSED u8 localIndex) {
    return NULL;
}

void network_reset_reconnect_and_rehost(void) {
}

void network_reconnect_begin(void) {
}

bool network_is_reconnecting(void) {
    return false;
}

void network_rehost_begin(void) {
}

bool network_allow_mod_dev_mode(void) {
    return false;
}

void network_mod_dev_mode_reload(void) {
}

void network_restart_game(void) {
    sNetworkRestarting = true;
    extern bool gDjuiInMainMenu;
    gDjuiInMainMenu = false;

    dynos_restart_reset();
    network_shutdown(false, false, false, false);
    dynos_restart_reset();
    network_init(NT_NONE, false);

    dynos_gfx_init();
    enable_queued_dynos_packs();

    gDjuiInMainMenu = false;
    extern s16 gChangeLevel;
    extern s16 gChangeLevelTransition;
    gChangeLevel = gLevelValues.entryLevel;
    gChangeLevelTransition = -1;

    sNetworkRestarting = false;
}

void network_update(void) {
    network_apply_config_to_server_settings();
}

void network_shutdown(UNUSED bool sendLeaving, bool exiting, UNUSED bool popup, UNUSED bool reconnecting) {
    gNetworkSentJoin = false;
    gNetworkServerAddr = NULL;
    gNetworkPlayerServer = &gNetworkPlayers[0];
    gNetworkType = NT_NONE;

    if (exiting) { return; }

    dynos_model_clear_pool(MODEL_POOL_SESSION);

    extern u8* gOverrideEeprom;
    gOverrideEeprom = NULL;
    extern u8 gOverrideFreezeCamera;
    gOverrideFreezeCamera = false;
    gDjuiHudLockMouse = false;
    gOverrideNear = 0;
    gOverrideFar = 0;
    gOverrideFOV = 0;
    gRoomOverride = -1;
    gCurrActStarNum = 0;
    gCurrActNum = 0;
    gCurrCreditsEntry = NULL;
    vec3f_set(gLightingDir, 0, 0, 0);
    color_set(gLightingColor[0], 0xFF, 0xFF, 0xFF);
    color_set(gLightingColor[1], 0xFF, 0xFF, 0xFF);
    color_set(gVertexColor, 0xFF, 0xFF, 0xFF);
    color_set(gSkyboxColor, 0xFF, 0xFF, 0xFF);
    color_set(gFogColor, 0xFF, 0xFF, 0xFF);
    gFogIntensity = 1.0f;
    gOverrideBackground = -1;
    gOverrideEnvFx = ENVFX_MODE_NO_OVERRIDE;
    gRomhackCameraSettings.centering = FALSE;
    gOverrideAllowToxicGasCamera = FALSE;
    gRomhackCameraSettings.dpad = FALSE;
    camera_reset_overrides();
    romhack_camera_reset_settings();
    free_vtx_scroll_targets();
    dynos_mod_shutdown();
    mods_clear(&gActiveMods);
    mods_clear(&gRemoteMods);
    smlua_shutdown();

    extern s16 gChangeLevel;
    if (!sNetworkRestarting) {
        gChangeLevel = LEVEL_CASTLE_GROUNDS;
    }

    network_player_init();
    gMarioStates[0].cap = 0;
    gMarioStates[0].input = 0;
    extern s16 gTTCSpeedSetting;
    gTTCSpeedSetting = 0;
    gOverrideDialogPos = 0;
    gOverrideDialogColor = 0;
    gDialogMinWidth = 0;
    gOverrideAllowToxicGasCamera = FALSE;
    gLuaVolumeMaster = 127;
    gLuaVolumeLevel = 127;
    gLuaVolumeSfx = 127;
    gLuaVolumeEnv = 127;

    struct Controller* cnt = gPlayer1Controller;
    cnt->rawStickX = 0;
    cnt->rawStickY = 0;
    cnt->stickX = 0;
    cnt->stickY = 0;
    cnt->stickMag = 0;
    cnt->buttonDown = 0;
    cnt->buttonPressed = 0;
    cnt->buttonReleased = 0;
    cnt->extStickX = 0;
    cnt->extStickY = 0;

    gFirstPersonCamera.enabled = false;
    gFirstPersonCamera.forcePitch = false;
    gFirstPersonCamera.forceYaw = false;
    gFirstPersonCamera.forceRoll = true;
    gFirstPersonCamera.centerL = true;
    gFirstPersonCamera.fov = FIRST_PERSON_DEFAULT_FOV;
    vec3f_set(gFirstPersonCamera.offset, 0, 0, 0);
    first_person_reset();

    le_shutdown();

    save_file_load_all(TRUE);
    save_file_set_using_backup_slot(false);
    f_shutdown();

    extern s16 gMenuMode;
    gMenuMode = -1;

    reset_window_title();

    init_mario_from_save_file();

    djui_panel_shutdown();
    if (!sNetworkRestarting) {
        if (!gDjuiInMainMenu) {
            gDjuiInMainMenu = true;
            djui_panel_main_create(NULL);
        }
    }
    djui_lua_error_clear();

    packet_ordered_clear_all();

    djui_reset_popup_disabled_override();
}

u8 network_global_index_from_local(u8 localIndex) {
    return (localIndex == 0) ? 0 : UNKNOWN_GLOBAL_INDEX;
}

u8 network_local_index_from_global(u8 globalIndex) {
    return (globalIndex == 0) ? 0 : UNKNOWN_LOCAL_INDEX;
}

bool network_is_server(void) {
    return false;
}

bool network_is_moderator(void) {
    return gNetworkPlayers[0].moderator;
}

u8* network_get_player_text_color(u8 localIndex) {
    if (localIndex >= MAX_PLAYERS) { localIndex = 0; }

    struct NetworkPlayer* np = &gNetworkPlayers[localIndex];
    static u8 sTextRgb[3] = { 0 };
    for (int i = 0; i < 3; i++) {
        sTextRgb[i] = 127 + np->overridePalette.parts[CAP][i] / 2;
    }

    return sTextRgb;
}

const char* network_get_player_text_color_string(u8 localIndex) {
    if (localIndex >= MAX_PLAYERS) { localIndex = 0; }
    u8* rgb = network_get_player_text_color(localIndex);
    static char sColorString[10] = { 0 };
    snprintf(sColorString, 10, "\\#%02x%02x%02x\\", rgb[0], rgb[1], rgb[2]);
    return sColorString;
}

extern s16 gMenuMode;
bool network_check_singleplayer_pause(void) {
    return ((gMenuMode != -1) || (gCameraMovementFlags & CAM_MOVE_PAUSE_SCREEN)) &&
        !gDjuiInPlayerMenu && mods_get_all_pausable();
}

const char* network_discord_id_from_local_index(u8 localIndex) {
    if (localIndex >= MAX_PLAYERS) { return "0"; }
    return gNetworkPlayers[localIndex].discordId;
}

bool network_player_name_valid(char* buffer) {
    if (buffer == NULL) { return false; }
    return buffer[0] != '\0';
}

void network_player_init(void) {
    network_init(NT_NONE, false);
}

void network_player_update_model(u8 localIndex) {
    if (localIndex >= MAX_PLAYERS) { return; }

    struct MarioState* m = &gMarioStates[localIndex];
    if (m == NULL) { return; }
    struct NetworkPlayer* np = &gNetworkPlayers[localIndex];

    u8 index = np->overrideModelIndex;
    if (index >= CT_MAX) { index = 0; }
    m->character = &gCharacters[index];

    if (m->marioObj == NULL || m->marioObj->behavior != bhvMario) { return; }
    obj_set_model(m->marioObj, m->character->modelId);
}

bool network_player_any_connected(void) {
    return true;
}

u8 network_player_connected_count(void) {
    return 1;
}

void network_player_set_description(struct NetworkPlayer* np, const char* description, u8 r, u8 g, u8 b, u8 a) {
    if (np == NULL) { return; }

    if (description != NULL) {
        snprintf(np->description, MAX_DESCRIPTION_STRING, "%s", description);
    } else {
        np->description[0] = '\0';
    }

    np->descriptionR = r;
    np->descriptionG = g;
    np->descriptionB = b;
    np->descriptionA = a;
}

void network_player_set_override_location(struct NetworkPlayer *np, const char *location) {
    if (np == NULL) { return; }

    if (location != NULL) {
        snprintf(np->overrideLocation, 256, "%s", location);
    } else {
        np->overrideLocation[0] = '\0';
    }
}

struct NetworkPlayer* network_player_from_global_index(u8 globalIndex) {
    return (globalIndex == 0) ? &gNetworkPlayers[0] : NULL;
}

struct NetworkPlayer* get_network_player_from_level(s16 courseNum, s16 actNum, s16 levelNum) {
    struct NetworkPlayer* np = &gNetworkPlayers[0];
    if (!np->connected) { return NULL; }
    if (np->currCourseNum != courseNum) { return NULL; }
    if (np->currActNum != actNum) { return NULL; }
    if (np->currLevelNum != levelNum) { return NULL; }
    return np;
}

struct NetworkPlayer* get_network_player_from_area(s16 courseNum, s16 actNum, s16 levelNum, s16 areaIndex) {
    struct NetworkPlayer* np = &gNetworkPlayers[0];
    if (!np->connected) { return NULL; }
    if (np->currCourseNum != courseNum) { return NULL; }
    if (np->currActNum != actNum) { return NULL; }
    if (np->currLevelNum != levelNum) { return NULL; }
    if (np->currAreaIndex != areaIndex) { return NULL; }
    return np;
}

struct NetworkPlayer* get_network_player_smallest_global(void) {
    return &gNetworkPlayers[0];
}

u8 network_player_get_palette_color_channel(struct NetworkPlayer *np, enum PlayerPart part, u8 index) {
    if (np == NULL) { return 0; }
    if (part >= PLAYER_PART_MAX) { return 0; }
    if (index >= 3) { return 0; }
    return np->palette.parts[part][index];
}

u8 network_player_get_override_palette_color_channel(struct NetworkPlayer *np, enum PlayerPart part, u8 index) {
    if (np == NULL) { return 0; }
    if (part >= PLAYER_PART_MAX) { return 0; }
    if (index >= 3) { return 0; }
    return np->overridePalette.parts[part][index];
}

void network_player_set_override_palette_color(struct NetworkPlayer *np, enum PlayerPart part, Color color) {
    if (np == NULL) { return; }
    if (part >= PLAYER_PART_MAX) { return; }

    np->overridePalette.parts[part][0] = color[0];
    np->overridePalette.parts[part][1] = color[1];
    np->overridePalette.parts[part][2] = color[2];
}

void network_player_reset_override_palette(struct NetworkPlayer *np) {
    if (np == NULL) { return; }
    np->overridePalette = np->palette;
}

bool network_player_is_override_palette_same(struct NetworkPlayer *np) {
    if (np == NULL) { return true; }
    return memcmp(&np->overridePalette, &np->palette, sizeof(struct PlayerPalette)) == 0;
}

void network_player_update(void) {
}

u8 network_player_connected(UNUSED enum NetworkPlayerType type, UNUSED u8 globalIndex, UNUSED u8 modelIndex, UNUSED const struct PlayerPalette* playerPalette, UNUSED const char* name, UNUSED const char* discordId) {
    return 0;
}

u8 network_player_disconnected(UNUSED u8 globalIndex) {
    return 0;
}

void construct_player_popup(UNUSED struct NetworkPlayer* np, UNUSED char* msg, UNUSED const char* level) {
}

void network_player_update_course_level(struct NetworkPlayer* np, s16 courseNum, s16 actNum, s16 levelNum, s16 areaIndex) {
    if (np == NULL) { return; }

    np->currCourseNum = courseNum;
    np->currActNum = actNum;
    np->currLevelNum = levelNum;
    np->currAreaIndex = areaIndex;
    np->currLevelSyncValid = true;
    np->currAreaSyncValid = true;
}

void network_player_shutdown(UNUSED bool popup) {
}

void lag_compensation_clear(void) {
}

void lag_compensation_store(void) {
}

struct MarioState* lag_compensation_get_local_state(UNUSED struct NetworkPlayer* otherNp) {
    return &gMarioStates[0];
}

bool lag_compensation_get_local_state_ready(void) {
    return false;
}

u32 lag_compensation_get_local_state_index(void) {
    return 0;
}

void sync_objects_init_system(void) {
}

void sync_objects_update(void) {
}

void sync_objects_clear(void) {
}

void sync_object_forget(UNUSED u32 syncId) {
}

void sync_object_forget_last_reliable_packet(UNUSED u32 syncId) {
}

struct SyncObject* sync_object_init(UNUSED struct Object *o, UNUSED float maxSyncDistance) {
    return NULL;
}

void sync_object_init_field(UNUSED struct Object *o, UNUSED void* field) {
}

void sync_object_init_field_with_size(UNUSED struct Object *o, UNUSED void* field, UNUSED u8 size) {
}

struct SyncObject* sync_object_get(UNUSED u32 syncId) {
    return NULL;
}

struct SyncObject* sync_object_get_first(void) {
    return NULL;
}

struct SyncObject* sync_object_get_next(void) {
    return NULL;
}

struct Object* sync_object_get_object(UNUSED u32 syncId) {
    return NULL;
}

bool sync_object_is_initialized(UNUSED u32 syncId) {
    return false;
}

bool sync_object_is_owned_locally(UNUSED u32 syncId) {
    return false;
}

struct Packet* sync_object_get_last_reliable_packet(UNUSED u32 syncId) {
    return NULL;
}

void sync_object_override_object(UNUSED u32 syncId, UNUSED struct Object* o) {
}

float player_distance(UNUSED struct MarioState* marioState, UNUSED struct Object* o) {
    return 0.0f;
}

bool sync_object_should_own(UNUSED u32 syncId) {
    return false;
}

bool sync_object_set_id(UNUSED struct Object* o) {
    return true;
}

void network_forget_all_reliable(void) {
}

void network_forget_all_reliable_from(UNUSED u8 localIndex) {
}

void network_send_ack(UNUSED struct Packet* p) {
}

void network_receive_ack(UNUSED struct Packet* p) {
}

void network_remember_reliable(UNUSED struct Packet* p) {
}

void network_update_reliable(void) {
}

void packet_compress(UNUSED struct Packet* p, UNUSED u8** compBuffer, UNUSED u32* compSize) {
}

bool packet_decompress(UNUSED struct Packet* p, UNUSED u8* compBuffer, UNUSED u32 compSize) {
    return false;
}

void packet_process(UNUSED struct Packet* p) {
}

void packet_receive(UNUSED struct Packet* packet) {
}

bool packet_spoofed(UNUSED struct Packet* p, UNUSED u8 globalIndex) {
    return false;
}

void packet_init(UNUSED struct Packet* packet, UNUSED enum PacketType packetType, UNUSED bool reliable, UNUSED enum PacketLevelMatchType levelAreaMustMatch) {
}

void packet_duplicate(UNUSED struct Packet* srcPacket, UNUSED struct Packet* dstPacket) {
}

void packet_set_flags(UNUSED struct Packet* packet) {
}

void packet_set_destination(UNUSED struct Packet* packet, UNUSED u8 destGlobalId) {
}

void packet_write(UNUSED struct Packet* packet, UNUSED void* data, UNUSED u16 length) {
}

u8 packet_initial_read(UNUSED struct Packet* packet) {
    return 0;
}

void packet_read(UNUSED struct Packet* packet, UNUSED void* data, UNUSED u16 length) {
}

u32 packet_hash(UNUSED struct Packet* packet) {
    return 0;
}

bool packet_check_hash(UNUSED struct Packet* packet) {
    return true;
}

void packet_ordered_begin(void) {
}

void packet_ordered_end(void) {
}

void packet_ordered_clear_all(void) {
}

void packet_set_ordered_data(UNUSED struct Packet* packet) {
}

void packet_ordered_add(UNUSED struct Packet* p) {
}

void packet_ordered_clear_table(UNUSED u8 globalIndex, UNUSED u16 groupdId) {
}

void packet_ordered_clear(UNUSED u8 globalIndex) {
}

void packet_ordered_update(void) {
}

u8 gAllowOrderedPacketClear = 0;

void network_update_player(void) {
}

void network_receive_player(UNUSED struct Packet* p) {
}

void network_send_object(UNUSED struct Object* o) {
}

void network_send_object_reliability(UNUSED struct Object* o, UNUSED bool reliable) {
}

void network_receive_object(UNUSED struct Packet* p) {
}

void network_update_objects(void) {
}

void network_send_spawn_objects(UNUSED struct Object* objects[], UNUSED u32 models[], UNUSED u8 objectCount) {
}

void network_send_spawn_objects_to(UNUSED u8 sendToLocalIndex, UNUSED struct Object* objects[], UNUSED u32 models[], UNUSED u8 objectCount) {
}

void network_receive_spawn_objects(UNUSED struct Packet* p) {
}

void network_send_spawn_star(UNUSED struct Object* o, UNUSED u8 starType, UNUSED f32 x, UNUSED f32 y, UNUSED f32 z, UNUSED u32 behParams, UNUSED u8 networkPlayerIndex) {
}

void network_receive_spawn_star(UNUSED struct Packet* p) {
}

void network_send_spawn_star_nle(UNUSED struct Object* o, UNUSED u32 params) {
}

void network_receive_spawn_star_nle(UNUSED struct Packet* p) {
}

void network_send_collect_star(UNUSED struct Object* o, UNUSED s16 coinScore, UNUSED s16 starIndex) {
}

void network_receive_collect_star(UNUSED struct Packet* p) {
}

void network_send_collect_coin(UNUSED struct Object* o) {
}

void network_receive_collect_coin(UNUSED struct Packet* p) {
}

void network_send_collect_item(UNUSED struct Object* o) {
}

void network_receive_collect_item(UNUSED struct Packet* p) {
}

void network_send_join_request(void) {
}

void network_receive_join_request(UNUSED struct Packet* p) {
}

void network_send_join(UNUSED struct Packet* joinRequestPacket) {
}

void network_receive_join(UNUSED struct Packet* p) {
}

u8 network_register_custom_packet(UNUSED void (*send_callback)(struct Packet* p, void* params), UNUSED void (*receive_callback)(struct Packet* p)) {
    return 0;
}

void network_send_custom(UNUSED u8 customId, UNUSED bool reliable, UNUSED enum PacketLevelMatchType levelAreaMustMatch, UNUSED void* params) {
}

void network_receive_custom(UNUSED struct Packet* p) {
}

void network_send_chat(UNUSED char* message, UNUSED u8 globalIndex) {
}

void network_receive_chat(UNUSED struct Packet* p) {
}

void network_send_kick(UNUSED u8 localIndex, UNUSED enum KickReasonType kickReason) {
}

void network_receive_kick(UNUSED struct Packet* p) {
}

void network_send_chat_command(UNUSED u8 localIndex, UNUSED enum ChatConfirmCommand CCC) {
}

void network_receive_chat_command(UNUSED struct Packet* p) {
}

void network_send_moderator(UNUSED u8 localIndex) {
}

void network_receive_moderator(UNUSED struct Packet* p) {
}

void network_send_keep_alive(UNUSED u8 localIndex) {
}

void network_receive_keep_alive(UNUSED struct Packet* p) {
}

void network_send_leaving(UNUSED u8 globalIndex) {
}

void network_receive_leaving(UNUSED struct Packet* p) {
}

void network_send_save_file(UNUSED s32 fileIndex) {
}

void network_receive_save_file(UNUSED struct Packet* p) {
}

void network_send_save_set_flag(UNUSED s32 fileIndex, UNUSED s32 courseIndex, UNUSED u8 courseStars, UNUSED u32 flags) {
}

void network_receive_save_set_flag(UNUSED struct Packet* p) {
}

void network_send_save_remove_flag(UNUSED s32 fileIndex, UNUSED s32 courseIndex, UNUSED u8 courseStarsToRemove, UNUSED u32 flagsToRemove) {
}

void network_receive_save_remove_flag(UNUSED struct Packet* p) {
}

void network_send_network_players_request(void) {
}

void network_receive_network_players_request(UNUSED struct Packet* p) {
}

void network_send_network_players(UNUSED u8 exceptLocalIndex) {
}

void network_receive_network_players(UNUSED struct Packet* p) {
}

void network_send_death(void) {
}

void network_receive_death(UNUSED struct Packet* p) {
}

void network_send_ping(UNUSED struct NetworkPlayer* toNp) {
}

void network_receive_ping(UNUSED struct Packet* p) {
}

void network_receive_pong(UNUSED struct Packet* p) {
}

void network_send_change_level(void) {
}

void network_receive_change_level(UNUSED struct Packet* p) {
}

void network_send_change_area(void) {
}

void network_receive_change_area(UNUSED struct Packet* p) {
}

void network_send_level_area_request(UNUSED struct NetworkPlayer* fromNp, UNUSED struct NetworkPlayer* toNp) {
}

void network_receive_level_area_request(UNUSED struct Packet* p) {
}

void network_send_level_request(UNUSED struct NetworkPlayer* fromNp, UNUSED struct NetworkPlayer* toNp) {
}

void network_receive_level_request(UNUSED struct Packet* p) {
}

void network_send_level(UNUSED struct NetworkPlayer* toNp, UNUSED bool sendArea) {
}

void network_receive_level(UNUSED struct Packet* p) {
}

void network_send_area_request(UNUSED struct NetworkPlayer* fromNp, UNUSED struct NetworkPlayer* toNp) {
}

void network_receive_area_request(UNUSED struct Packet* p) {
}

void area_remove_sync_ids_add(UNUSED u32 syncId) {
}

void area_remove_sync_ids_clear(void) {
}

void network_send_area(UNUSED struct NetworkPlayer* toNp) {
}

void network_receive_area(UNUSED struct Packet* p) {
}

void network_send_sync_valid(UNUSED struct NetworkPlayer* toNp, UNUSED s16 courseNum, UNUSED s16 actNum, UNUSED s16 levelNum, UNUSED s16 areaIndex) {
}

void network_receive_sync_valid(UNUSED struct Packet* p) {
}

void network_send_level_spawn_info(UNUSED struct NetworkPlayer* destNp) {
}

void network_receive_level_spawn_info(UNUSED struct Packet* p) {
}

void network_send_level_macro(UNUSED struct NetworkPlayer* destNp) {
}

void network_receive_level_macro(UNUSED struct Packet* p) {
}

void network_send_level_area_inform(void) {
}

void network_receive_level_area_inform(UNUSED struct Packet* p) {
}

void network_send_level_respawn_info(UNUSED struct Object* o, UNUSED u8 respawnInfoBits) {
}

void network_receive_level_respawn_info(UNUSED struct Packet* p) {
}

void network_send_change_water_level(UNUSED u8 index, UNUSED s16 height) {
}

void network_receive_change_water_level(UNUSED struct Packet* p) {
}

void network_send_debug_sync(void) {
}

void network_receive_debug_sync(UNUSED struct Packet* p) {
}

void network_send_player_settings(void) {
}

void network_receive_player_settings(UNUSED struct Packet* p) {
}

void network_send_mod_list_request(void) {
}

void network_receive_mod_list_request(UNUSED struct Packet* p) {
}

void network_send_mod_list(void) {
}

void network_receive_mod_list(UNUSED struct Packet* p) {
}

void network_receive_mod_list_entry(UNUSED struct Packet* p) {
}

void network_receive_mod_list_file(UNUSED struct Packet* p) {
}

void network_receive_mod_list_done(UNUSED struct Packet* p) {
}

void network_start_download_requests(void) {
}

void network_send_next_download_request(void) {
}

void network_send_download_request(UNUSED u64 offset) {
}

void network_receive_download_request(UNUSED struct Packet* p) {
}

void network_send_download(UNUSED u64 offset) {
}

void network_receive_download(UNUSED struct Packet* p) {
}

void network_send_global_popup(UNUSED const char* message, UNUSED int lines) {
}

void network_receive_global_popup(UNUSED struct Packet* p) {
}

void network_send_lua_sync_table_request(void) {
}

void network_receive_lua_sync_table_request(UNUSED struct Packet* p) {
}

void network_send_lua_sync_table(UNUSED u8 toLocalIndex, UNUSED u64 seq, UNUSED u16 remoteIndex, UNUSED u16 lntKeyCount, UNUSED struct LSTNetworkType* lntKey, UNUSED struct LSTNetworkType* lntValue) {
}

void network_receive_lua_sync_table(UNUSED struct Packet* p) {
}

void network_send_request_failed(UNUSED struct NetworkPlayer* toNp, UNUSED u8 requestType) {
}

void network_receive_request_failed(UNUSED struct Packet* p) {
}

void network_send_lua_custom(UNUSED bool broadcast) {
}

void network_receive_lua_custom(UNUSED struct Packet* p) {
}

void network_send_lua_custom_bytestring(UNUSED bool broadcast) {
}

void network_receive_lua_custom_bytestring(UNUSED struct Packet* p) {
}
