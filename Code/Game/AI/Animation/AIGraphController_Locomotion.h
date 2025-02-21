#pragma once
#include "Engine/Animation/Graph/Animation_RuntimeGraph_Controller.h"

//-------------------------------------------------------------------------

namespace EE::AI
{
    class LocomotionGraphController final : public Animation::SubGraphController
    {
    public:

        EE_ANIMATION_SUBGRAPH_CONTROLLER_ID( LocomotionGraphController );

    public:

        LocomotionGraphController( Animation::GraphInstance* pGraphInstance, Render::SkeletalMeshComponent* pMeshComponent );

        // Request the character go into an idle state
        void SetIdle();

        // Set movement parameters - facing direction will be converted to 2D
        void SetLocomotionDesires( Seconds const deltaTime, Vector const& headingVelocityWS, Vector const& facingDirectionWS );

    private:

        // 运动速度
        ControlParameter<float>     m_speedParam = ControlParameter<float>( "Locomotion_Speed" );
        // 运动前进方向
        ControlParameter<Vector>    m_headingParam = ControlParameter<Vector>( "Locomotion_Heading" );
        // 面向方向
        ControlParameter<Vector>    m_facingParam = ControlParameter<Vector>( "Locomotion_Facing" );
    };
}