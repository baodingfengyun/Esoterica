#pragma once

#include "Game/Player/Animation/PlayerGraphController_Locomotion.h"

//-------------------------------------------------------------------------

namespace EE::AI
{
    // 定义角色动作状态
    enum class CharacterAnimationState : uint8_t
    {
        Locomotion = 0,     // 运动
        Falling,            // 下落
        Ability,            // 某种能力

        DebugMode,          // 调试状态
        NumStates           // --最后一个用来定义数量(从 0 开始)
    };

    //-------------------------------------------------------------------------

    class AnimationController final : public Animation::GraphController
    {
    public:

        AnimationController( Animation::AnimationGraphComponent* pGraphComponent, Render::SkeletalMeshComponent* pMeshComponent );

        void SetCharacterState( CharacterAnimationState state );

        #if EE_DEVELOPMENT_TOOLS
        virtual char const* GetName() const { return "AI Graph Controller"; }
        #endif

    private:

        ControlParameter<StringID>     m_characterStateParam = "CharacterState";
    };
}