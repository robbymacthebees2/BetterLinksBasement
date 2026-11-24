#pragma once

#include "mods/svc/actor.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"

class BasementRedPotActor : public fopAc_ac_c {
public:
    static constexpr char kProfileName[] = "BLRedPt";
    static ProfileName s_profileName;
    static ActorHandle s_actorHandle;
    static const ActorProfileDesc s_profile;

    static void setRoomLightPosition(const cXyz& position);
    cPhs_Step create();
    void releaseResources();
    int Delete();
    int CreateHeap();
    int Execute();
    int Draw();
    static int createHeapCallBack(fopAc_ac_c* actor);

private:
    request_of_phase_process_class m_phase{};
    J3DModel* m_model = nullptr;
    dCcD_Stts m_collisionStatus{};
    dCcD_Cyl m_collisionTarget{};
    dCcD_Cyl m_collisionCorrection{};
    f32 m_collisionHalfWidthX = 0.0f;
    f32 m_collisionHalfWidthZ = 0.0f;
    f32 m_collisionHeight = 0.0f;
    cXyz m_collisionCenterOffset{};
    static cXyz s_roomLightPosition;
};
