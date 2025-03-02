#pragma once
#include "System/Algorithm/Hash.h"
#include "Engine/Entity/EntityWorldUpdateContext.h"

#if EE_DEVELOPMENT_TOOLS
#include "imgui.h"
#endif

//-------------------------------------------------------------------------

namespace EE
{
    class EntityComponent;
    namespace Physics { class CharacterComponent; class Scene; }
    namespace Navmesh { class NavmeshWorldSystem; }
    namespace Drawing { class DrawContext; }
}

//-------------------------------------------------------------------------

namespace EE::AI
{
    class AIComponent;
    class CharacterPhysicsController;
    class AnimationController;

    //-------------------------------------------------------------------------
    // The context for all AI behaviors
    // 所有 AI 行为的上下文 (结构体)
    //-------------------------------------------------------------------------
    // Provides the common set of systems and components needed for AI behaviors/actions

    struct BehaviorContext
    {
        ~BehaviorContext();

        bool IsValid() const;

        template<typename T>
        T* GetComponentByType() const
        {
            static_assert( std::is_base_of<EntityComponent, T>::value, "T must be a component type" );
            for ( auto pComponent : m_components )
            {
                if ( auto pCastComponent = TryCast<T>( pComponent ) )
                {
                    return pCastComponent;
                }
            }

            return nullptr;
        }

        // Forwarding helper functions
        //-------------------------------------------------------------------------

        EE_FORCE_INLINE Seconds GetDeltaTime() const { return m_pEntityWorldUpdateContext->GetDeltaTime(); }
        template<typename T> inline T* GetWorldSystem() const { return m_pEntityWorldUpdateContext->GetWorldSystem<T>(); }
        template<typename T> inline T* GetSystem() const { return m_pEntityWorldUpdateContext->GetSystem<T>(); }
        template<typename T> inline T* GetAnimSubGraphController() const { return m_pAnimationController->GetSubGraphController<T>(); }

        #if EE_DEVELOPMENT_TOOLS
        Drawing::DrawContext GetDrawingContext() const;
        #endif

    public:

        //----------------- 这里定义了实体世界更新上下文, 物理场景, 导航网格, AI 组件,
        //----------------- 物理角色组件, 角色物理管理器, 动画管理器, 实体组件列表

        EntityWorldUpdateContext const*             m_pEntityWorldUpdateContext = nullptr;
        Physics::Scene*                             m_pPhysicsScene = nullptr;
        Navmesh::NavmeshWorldSystem*                m_pNavmeshSystem = nullptr;

        AIComponent*                                m_pAIComponent = nullptr;
        Physics::CharacterComponent*                m_pCharacter = nullptr;
        CharacterPhysicsController*                 m_pCharacterController = nullptr;
        AnimationController*                        m_pAnimationController = nullptr;
        TInlineVector<EntityComponent*, 10>         m_components;
    };

    //-------------------------------------------------------------------------
    // An AI Action 一个 AI 动作,执行实际的任务(移动,播放动画等等),为了达到某个目标.
    //-------------------------------------------------------------------------
    // A specific actuation task (move, play anim, etc...) that a behavior requests to help achieve its goal
    // Each derived action needs to define a StartInternal( ActionContext const& ctx, ARGS... ) function that will start the AI action

    class Action
    {
    public:

        // 动作状态: 运行中, 完成, 失败
        enum class Status : uint8_t
        {
            Running,
            Completed,
            Failed
        };

    public:

    };


    //-------------------------------------------------------------------------
    // An AI behavior 一个 AI 行为,例如一系列顺序执行达到某个目标的动作
    //-------------------------------------------------------------------------
    // This defines a behavior i.e. a sequence of actions to achieve a specified goal

    class Behavior
    {
    public:

        // 行为状态: 运行中, 完成, 失败
        enum class Status : uint8_t
        {
            Running,
            Completed,
            Failed
        };

        // 停止原因: 已完成, 被中断
        enum class StopReason : uint8_t
        {
            Completed,
            Interrupted
        };

    public:

        virtual ~Behavior() = default;

        // Get the ID for this action
        virtual uint32_t GetActionID() const = 0;

        // Is this action active
        inline bool IsActive() const { return m_isActive; }

        // Try to start this action - this is where you check all the start preconditions
        inline void Start( BehaviorContext const& ctx )
        {
            EE_ASSERT( !m_isActive );
            StartInternal( ctx );
            m_isActive = true;
        }

        // Called to update this action, this will be called directly after the try start if it succeeds
        inline Status Update( BehaviorContext const& ctx )
        {
            EE_ASSERT( m_isActive );
            return UpdateInternal( ctx );
        }

        // Called to stop this action
        inline void Stop( BehaviorContext const& ctx, StopReason reason )
        {
            EE_ASSERT( m_isActive );
            StopInternal( ctx, reason );
            m_isActive = false;
        }

        //-------------------------------------------------------------------------

        #if EE_DEVELOPMENT_TOOLS
        // Returns a friendly name for the action
        virtual char const* GetName() const = 0;

        // Override this function to draw custom imgui controls in the action debugger UI
        virtual void DrawDebugUI() { ImGui::Text( "No Debug" ); }
        #endif

    protected:

        // Called to start this action
        virtual void StartInternal( BehaviorContext const& ctx ) = 0;

        // Called to update this action, this will be called directly after the try start if it succeeds
        virtual Status UpdateInternal( BehaviorContext const& ctx ) = 0;

        // Called to stop this action
        virtual void StopInternal( BehaviorContext const& ctx, StopReason reason ) = 0;

    private:
        // 是否活跃
        bool    m_isActive = false;
    };
}

//-------------------------------------------------------------------------

#define EE_AI_BEHAVIOR_ID( TypeName ) \
constexpr static uint32_t const s_gameplayStateID = Hash::FNV1a::GetHash32( #TypeName ); \
virtual uint32_t GetActionID() const override final { return TypeName::s_gameplayStateID; }\
EE_DEVELOPMENT_TOOLS_LINE_IN_MACRO( char const* GetName() const override final { return #TypeName; } )