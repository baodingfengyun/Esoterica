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

    // 动画控制器
    class AnimationController final : public Animation::GraphController
    {
    public:

        // 构造函数(动画图组件, 骨架组件)
        AnimationController( Animation::AnimationGraphComponent* pGraphComponent, Render::SkeletalMeshComponent* pMeshComponent );

        // 设置角色状态
        void SetCharacterState( CharacterAnimationState state );

        // EE开发工具可获取控制器名字
        #if EE_DEVELOPMENT_TOOLS
        virtual char const* GetName() const { return "AI Graph Controller"; }
        #endif

    private:

        ControlParameter<StringID>     m_characterStateParam = "CharacterState";
    };
}