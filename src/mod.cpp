#include "mods/service.hpp"
#include "mods/svc/actor.h"
#include "mods/svc/log.hpp"

#include "basement_red_pot.hpp"
#include "d/actor/d_a_obj_lv2Candle.h"
#include "d/actor/d_a_obj_oiltubo.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_kankyo_tev_str.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_manager.h"
#include "mods/svc/hook.hpp"
#include "SSystem/SComponent/c_math.h"

DEFINE_MOD();

IMPORT_SERVICE(ActorService, svc_actor);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, svc_log);

DEFINE_HOOK(&dScnKy_env_light_c::setLight_bg, BasementBackgroundLighting);
DEFINE_HOOK(&dScnKy_env_light_c::settingTevStruct, BasementRoomGeometryLighting);
DEFINE_HOOK(
    static_cast<void (dScnKy_env_light_c::*)(J3DModelData*, dKy_tevstr_c*)>(
        &dScnKy_env_light_c::setLightTevColorType_MAJI),
    BasementTorchMaterialLighting);

namespace {

constexpr char kBasementStage[] = "R_SP01";
constexpr s8 kAdjacentRoom = 4;
constexpr s8 kBasementRoom = 7;
constexpr size_t kReplacementActorCount = 3;
constexpr u32 kCandleParameters = 0x000010FFu;
constexpr f32 kRedPotScale = 1.6f;
constexpr f32 kRedPotWallClearance = 30.0f;

bool s_installed = false;
ActorId s_spawnedActors[kReplacementActorCount]{};
bool s_spawnedActorsLayered[kReplacementActorCount]{};

struct LightingHookState {
    bool background = false;
    bool torchMaterial = false;
    bool roomGeometry = false;
};

LightingHookState s_lightingHooks;

struct ChestSearch {
    s16 profile;
};

void* is_basement_chest(void* actor, void* data) {
    auto* chest = static_cast<fopAc_ac_c*>(actor);
    auto* search = static_cast<ChestSearch*>(data);
    if (fopAcM_GetName(chest) == search->profile &&
        fopAcM_GetRoomNo(chest) == kBasementRoom) {
        return chest;
    }
    return nullptr;
}

bool in_basement() {
    if (std::strcmp(dComIfGp_getStartStageName(), kBasementStage) != 0) {
        return false;
    }

    const fopAc_ac_c* player = dComIfGp_getPlayer(0);
    return player != nullptr && fopAcM_GetRoomNo(player) == kBasementRoom &&
        dComIfGp_roomControl_getStayNo() == kBasementRoom;
}

void lift_fullbright_ambient(GXColorS10& ambient) {
    if (ambient.r < 128) {
        ambient.r = 128;
    }
    if (ambient.g < 96) {
        ambient.g = 96;
    }
    if (ambient.b < 64) {
        ambient.b = 64;
    }
}

void lift_player_ambient(GXColorS10& ambient) {
    if (ambient.r < 128) {
        ambient.r = 128;
    }
    if (ambient.g < 104) {
        ambient.g = 104;
    }
    if (ambient.b < 80) {
        ambient.b = 80;
    }
}

bool basement_room_tev(const dKy_tevstr_c* tevStr) {
    const s8 stayRoom = dComIfGp_roomControl_getStayNo();
    if (std::strcmp(dComIfGp_getStartStageName(), kBasementStage) != 0 ||
        (stayRoom != kBasementRoom && stayRoom != kAdjacentRoom) || tevStr == nullptr) {
        return false;
    }
    return tevStr->room_no == kBasementRoom;
}

void lift_background_ambient(ModContext*, void* args, void*, void*) {
    auto* tevStr = mods::arg<dKy_tevstr_c*>(args, 1);
    auto* ambient = mods::arg<GXColorS10*>(args, 2);
    if (basement_room_tev(tevStr) && tevStr->Type == 8 && ambient != nullptr) {
        for (size_t i = 0; i < 4; ++i) {
            lift_fullbright_ambient(ambient[i]);
        }
    }
}

void lift_room_geometry_ambient(ModContext*, void* args, void*, void*) {
    auto* tevStr = mods::arg<dKy_tevstr_c*>(args, 3);
    if (basement_room_tev(tevStr) && tevStr->Type == 0x40) {
        lift_fullbright_ambient(tevStr->AmbCol);
    }
}

ModResult remove_lighting_hooks() {
    ModResult firstError = MOD_OK;
    auto uninstall = [&firstError](bool& installed, auto hookTag) {
        if (!installed) {
            return;
        }
        const ModResult result = mods::hook::uninstall<decltype(hookTag)>();
        if (result == MOD_OK) {
            installed = false;
        } else if (firstError == MOD_OK) {
            firstError = result;
        }
    };

    uninstall(s_lightingHooks.roomGeometry, BasementRoomGeometryLighting{});
    uninstall(s_lightingHooks.torchMaterial, BasementTorchMaterialLighting{});
    uninstall(s_lightingHooks.background, BasementBackgroundLighting{});
    return firstError;
}

HookAction lift_torch_ambient(ModContext*, void* args, void*, void*) {
    auto* tevStr = mods::arg<dKy_tevstr_c*>(args, 2);
    if (tevStr == nullptr || std::strcmp(dComIfGp_getStartStageName(), kBasementStage) != 0) {
        return HOOK_CONTINUE;
    }

    const s8 stayRoom = dComIfGp_roomControl_getStayNo();
    const fopAc_ac_c* player = dComIfGp_getPlayer(0);
    const bool isPlayerTev = player != nullptr && tevStr == &player->tevStr;
    if (stayRoom == kBasementRoom && player != nullptr &&
        fopAcM_GetRoomNo(player) == kBasementRoom && isPlayerTev) {
        lift_player_ambient(tevStr->AmbCol);
    } else if (!isPlayerTev && basement_room_tev(tevStr) &&
               (tevStr->Type == 0x10 || (tevStr->Type >= 32 && tevStr->Type <= 35))) {
        lift_fullbright_ambient(tevStr->AmbCol);
    }
    return HOOK_CONTINUE;
}

fopAc_ac_c* find_basement_chest() {
    constexpr s16 chestProfiles[] = {fpcNm_TBOX_e, fpcNm_TBOX2_e};

    for (s16 profile : chestProfiles) {
        ChestSearch search{profile};
        auto* chest =
            static_cast<fopAc_ac_c*>(fopAcM_Search(is_basement_chest, &search));
        if (chest != nullptr) {
            return chest;
        }
    }
    return nullptr;
}

bool place_basement_items(fopAc_ac_c* chest) {
    const cXyz chestPosition = chest->current.pos;
    const csXyz chestAngle = chest->shape_angle;

    cXyz oilPosition = chestPosition;
    oilPosition.x -= 120.0f;

    cXyz potionPosition = chestPosition;
    potionPosition.x += 120.0f;
    potionPosition.x += kRedPotWallClearance * cM_ssin(chestAngle.y);
    potionPosition.z += kRedPotWallClearance * cM_scos(chestAngle.y);

    cXyz torchPosition = chestPosition;
    torchPosition.z -= 160.0f;
    const cXyz roomLightPosition(
        torchPosition.x, torchPosition.y + 145.0f, torchPosition.z);
    BasementRedPotActor::setRoomLightPosition(roomLightPosition);

    ActorSpawnParams oil{
        .parameters = 0,
        .argument = -1,
        .room_num = kBasementRoom,
        .position = {oilPosition.x, oilPosition.y, oilPosition.z},
        .angle = {0, chestAngle.y, 0},
        .scale = {1.0f, 1.0f, 1.0f},
    };

    ActorSpawnParams redPot{
        .parameters = 0,
        .argument = -1,
        .room_num = kBasementRoom,
        .position = {potionPosition.x, potionPosition.y, potionPosition.z},
        .angle = {0, chestAngle.y, 0},
        .scale = {kRedPotScale, kRedPotScale, kRedPotScale},
    };

    ActorSpawnParams candle{
        .parameters = kCandleParameters,
        .argument = -1,
        .room_num = kBasementRoom,
        .position = {torchPosition.x, chestPosition.y, torchPosition.z},
        .angle = {0, chestAngle.y, 0},
        .scale = {1.0f, 1.0f, 1.0f},
    };

    constexpr const char* actorNames[] = {"OilTubo", nullptr, "candlL2"};
    const ActorSpawnParams* spawnParams[] = {&oil, &redPot, &candle};
    ActorId spawnedActors[kReplacementActorCount]{};
    size_t spawnedCount = 0;

    for (size_t i = 0; i < kReplacementActorCount; ++i) {
        const ModResult result = i == 1
            ? svc_actor->create_actor(mod_ctx, BasementRedPotActor::s_profileName,
                  spawnParams[i], &spawnedActors[spawnedCount])
            : svc_actor->create_actor_from_name(
                  mod_ctx, actorNames[i], spawnParams[i], &spawnedActors[spawnedCount]);
        if (result != MOD_OK) {
            while (spawnedCount > 0) {
                --spawnedCount;
                svc_actor->delete_actor(mod_ctx, spawnedActors[spawnedCount]);
            }
            mods::log::error("Failed to spawn basement replacement actor {}", actorNames[i]);
            return false;
        }
        ++spawnedCount;
    }

    const ModResult deleteResult = svc_actor->delete_actor(mod_ctx, fopAcM_GetID(chest));
    if (deleteResult != MOD_OK) {
        while (spawnedCount > 0) {
            --spawnedCount;
            svc_actor->delete_actor(mod_ctx, spawnedActors[spawnedCount]);
        }
        mods::log::error("Failed to remove the basement chest");
        return false;
    }

    for (size_t i = 0; i < spawnedCount; ++i) {
        s_spawnedActors[i] = spawnedActors[i];
        s_spawnedActorsLayered[i] = false;
    }
    mods::log::info("Added the basement jars and lit candle");
    return true;
}

void assign_replacements_to_room() {
    for (size_t i = 0; i < kReplacementActorCount; ++i) {
        if (s_spawnedActorsLayered[i]) {
            continue;
        }

        fopAc_ac_c* actor = fopAcM_SearchByID(s_spawnedActors[i]);
        if (actor != nullptr) {
            fopAcM_setRoomLayer(actor, kBasementRoom);
            s_spawnedActorsLayered[i] = true;
        }
    }
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError*) {
    const ModResult lightingResult =
        mods::hook::add_post<BasementBackgroundLighting>(lift_background_ambient);
    if (lightingResult != MOD_OK) {
        mods::log::error("Failed to install basement background lighting hook");
        return lightingResult;
    }
    s_lightingHooks.background = true;

    const ModResult torchLightingResult =
        mods::hook::add_pre<BasementTorchMaterialLighting>(lift_torch_ambient);
    if (torchLightingResult != MOD_OK) {
        const ModResult cleanupResult = remove_lighting_hooks();
        if (cleanupResult != MOD_OK) {
            mods::log::error("Failed to remove basement lighting hooks after installation error");
        }
        mods::log::error("Failed to install basement torch lighting hook");
        return torchLightingResult;
    }
    s_lightingHooks.torchMaterial = true;

    const ModResult roomLightingResult =
        mods::hook::add_post<BasementRoomGeometryLighting>(lift_room_geometry_ambient);
    if (roomLightingResult != MOD_OK) {
        const ModResult cleanupResult = remove_lighting_hooks();
        if (cleanupResult != MOD_OK) {
            mods::log::error("Failed to remove basement lighting hooks after installation error");
        }
        mods::log::error("Failed to install basement room geometry lighting hook");
        return roomLightingResult;
    }
    s_lightingHooks.roomGeometry = true;

    const ModResult registrationResult = svc_actor->register_actor(
        mod_ctx, &BasementRedPotActor::s_profile, &BasementRedPotActor::s_profileName,
        &BasementRedPotActor::s_actorHandle);
    if (registrationResult != MOD_OK) {
        const ModResult cleanupResult = remove_lighting_hooks();
        if (cleanupResult != MOD_OK) {
            mods::log::error("Failed to remove basement lighting hooks after actor registration error");
        }
        mods::log::error("Failed to register the static basement red pot actor");
        return registrationResult;
    }

    mods::log::info("Better Link's Basement initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (!in_basement()) {
        if (s_installed) {
            s_installed = false;
            for (size_t i = 0; i < kReplacementActorCount; ++i) {
                s_spawnedActors[i] = 0;
                s_spawnedActorsLayered[i] = false;
            }
        }
        return MOD_OK;
    }

    if (!s_installed) {
        fopAc_ac_c* chest = find_basement_chest();
        if (chest != nullptr) {
            s_installed = place_basement_items(chest);
        }
    }

    if (s_installed) {
        assign_replacements_to_room();
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    s_installed = false;
    for (size_t i = 0; i < kReplacementActorCount; ++i) {
        s_spawnedActors[i] = 0;
        s_spawnedActorsLayered[i] = false;
    }
    const ModResult hookCleanupResult = remove_lighting_hooks();
    if (hookCleanupResult != MOD_OK) {
        mods::log::error("Failed to remove basement lighting hooks");
        return hookCleanupResult;
    }
    return MOD_OK;
}

}
