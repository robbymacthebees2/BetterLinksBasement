#include "basement_red_pot.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_item_data.h"
#include "SSystem/SComponent/c_math.h"

#include <cstring>

namespace {

constexpr u32 kSolidHeapSize = 0x2000;
constexpr char kPotArchive[] = "O_mD_red";
constexpr char kPotModel[] = "o_md_red.bmd";
constexpr f32 kCollisionScale = 0.9f;
constexpr f32 kRoomLightRadius = 2400.0f;

const dCcD_SrcGObjInf kPotCollisionInfo = {
    {0x0, {{0x0, 0x0, 0x0}, {0xd8fbfdff, 0x1F}, 0x79}},
    {dCcD_SE_NONE, 0x0, 0x0, 0x0, 0x0},
    {dCcD_SE_NONE, 0x2, 0x0, 0x0, 0x1},
    {0x0},
};

const dCcD_SrcCyl kPotCollisionSource = {
    kPotCollisionInfo,
    {
        {
            {0.0f, 0.0f, 0.0f},
            0.0f,
            0.0f,
        },
    },
};

cPhs_Step create_actor(void* actor) {
    return static_cast<BasementRedPotActor*>(actor)->create();
}

int delete_actor(void* actor) {
    return static_cast<BasementRedPotActor*>(actor)->Delete();
}

int execute_actor(void* actor) {
    return static_cast<BasementRedPotActor*>(actor)->Execute();
}

int draw_actor(void* actor) {
    return static_cast<BasementRedPotActor*>(actor)->Draw();
}

int is_delete_actor(void*) {
    return 1;
}

}  // namespace

ProfileName BasementRedPotActor::s_profileName = -1;
ActorHandle BasementRedPotActor::s_actorHandle = 0;
cXyz BasementRedPotActor::s_roomLightPosition{};

const ActorProfileDesc BasementRedPotActor::s_profile = {
    .name = "BLRedPt",
    .priority_group = 7,
    .process_size = sizeof(BasementRedPotActor),
    .draw_priority = fpcDwPi_Obj_Carry_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = create_actor,
    .delete_function = delete_actor,
    .execute_function = execute_actor,
    .is_delete_function = is_delete_actor,
    .draw_function = draw_actor,
};

void BasementRedPotActor::setRoomLightPosition(const cXyz& position) {
    s_roomLightPosition = position;
}

cPhs_Step BasementRedPotActor::create() {
    fopAcM_ct(this, BasementRedPotActor);

    const cPhs_Step phase = dComIfG_resLoad(&m_phase, kPotArchive);
    if (phase == cPhs_COMPLEATE_e) {
        if (!fopAcM_entrySolidHeap(this, createHeapCallBack, kSolidHeapSize)) {
            return cPhs_ERROR_e;
        }

        fopAcM_SetMtx(this, m_model->getBaseTRMtx());
        fopAcM_setCullSizeBox2(this, m_model->getModelData());
        eventInfo.setArchiveName(kPotArchive);
        attention_info.flags = 0;
        m_collisionStatus.Init(0xFF, 0, this);
        m_collisionTarget.Set(kPotCollisionSource);
        m_collisionTarget.SetStts(&m_collisionStatus);
        m_collisionCorrection.Set(kPotCollisionSource);
        m_collisionCorrection.SetStts(&m_collisionStatus);
        Execute();
    }

    return phase;
}

void BasementRedPotActor::releaseResources() {
    dComIfG_resDelete(&m_phase, kPotArchive);
}

int BasementRedPotActor::Delete() {
    releaseResources();
    this->~BasementRedPotActor();
    return 1;
}

int BasementRedPotActor::CreateHeap() {
    auto* modelData =
        static_cast<J3DModelData*>(dComIfG_getObjectRes(kPotArchive, kPotModel));
    if (modelData == nullptr) {
        return 0;
    }

    m_model = mDoExt_J3DModel__create(modelData, 0x80000, 0x11000084);
    if (m_model == nullptr) {
        return 0;
    }

    J3DJoint* rootJoint = modelData->getJointNodePointer(0);
    if (rootJoint == nullptr) {
        return 0;
    }

    const Vec& modelMin = *rootJoint->getMin();
    const Vec& modelMax = *rootJoint->getMax();
    m_collisionHalfWidthX = (modelMax.x - modelMin.x) * 0.5f;
    m_collisionHalfWidthZ = (modelMax.z - modelMin.z) * 0.5f;
    m_collisionHeight = modelMax.y - modelMin.y;
    m_collisionCenterOffset =
        cXyz((modelMin.x + modelMax.x) * 0.5f, modelMin.y,
             (modelMin.z + modelMax.z) * 0.5f);
    return 1;
}

int BasementRedPotActor::createHeapCallBack(fopAc_ac_c* actor) {
    return static_cast<BasementRedPotActor*>(actor)->CreateHeap();
}

int BasementRedPotActor::Execute() {
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::ZXYrotM(shape_angle);
    mDoMtx_stack_c::scaleM(scale);
    m_model->setBaseTRMtx(mDoMtx_stack_c::get());
    attention_info.flags = 0;
    attention_info.position = current.pos;
    attention_info.position.y += 150.0f;
    eyePos = current.pos;

    const f32 collisionRadiusX = m_collisionHalfWidthX * scale.x * kCollisionScale;
    const f32 collisionRadiusZ = m_collisionHalfWidthZ * scale.z * kCollisionScale;
    const f32 collisionRadius =
        collisionRadiusX > collisionRadiusZ ? collisionRadiusX : collisionRadiusZ;
    cXyz collisionCenter = current.pos;
    const f32 localCenterX = m_collisionCenterOffset.x * scale.x;
    const f32 localCenterZ = m_collisionCenterOffset.z * scale.z;
    collisionCenter.x += localCenterZ * cM_ssin(shape_angle.y) +
        localCenterX * cM_scos(shape_angle.y);
    collisionCenter.y += m_collisionCenterOffset.y * scale.y;
    collisionCenter.z += localCenterZ * cM_scos(shape_angle.y) -
        localCenterX * cM_ssin(shape_angle.y);

    m_collisionStatus.SetRoomId(fopAcM_GetRoomNo(this));
    m_collisionTarget.SetR(collisionRadius);
    m_collisionTarget.SetH(m_collisionHeight * scale.y * kCollisionScale);
    m_collisionTarget.SetC(collisionCenter);
    dComIfG_Ccsp()->Set(&m_collisionTarget);

    m_collisionCorrection.SetCoSPrm(0);
    m_collisionCorrection.SetTgType(0xD8FBFDFF);
    m_collisionCorrection.SetTgSPrm(0x1F);
    m_collisionCorrection.SetR(collisionRadius);
    m_collisionCorrection.SetH(m_collisionHeight * scale.y * kCollisionScale);
    m_collisionCorrection.SetC(collisionCenter);
    dComIfG_Ccsp()->Set(&m_collisionCorrection);

    if (std::strcmp(dComIfGp_getStartStageName(), "R_SP01") == 0 &&
        dComIfGp_roomControl_getStayNo() == 7) {
        GXColor lightColor{0xBC, 0x66, 0x42, 0xFF};
        dKy_BossLight_set(&s_roomLightPosition, &lightColor, kRoomLightRadius, 0);
    }

    if (dComIfGp_getEvent()->isOrderOK()) {
        if (!eventInfo.checkCommandCatch()) {
            dComIfGp_att_CatchRequest(this, dItemNo_RED_BOTTLE_2_e, 100.0f, 50.0f,
                                      -50.0f, 0x2000, 1);
            eventInfo.onCondition(dEvtCnd_40_e);
        }
    }

    return 1;
}

int BasementRedPotActor::Draw() {
    g_env_light.settingTevStruct(8, &current.pos, &tevStr);
    g_env_light.setLightTevColorType_MAJI(m_model, &tevStr);
    dComIfGd_setListBG();
    mDoExt_modelUpdateDL(m_model);
    dComIfGd_setList();
    return 1;
}
