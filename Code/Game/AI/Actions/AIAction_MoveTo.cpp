#include "AIAction_MoveTo.h"
#include "Game/AI/Behaviors/AIBehavior.h"
#include "Game/AI/Animation/AIGraphController_Locomotion.h"
#include "Game/AI/Animation/AIAnimationController.h"
#include "Engine/Navmesh/Systems/WorldSystem_Navmesh.h"
#include "Engine/Physics/Components/Component_PhysicsCharacter.h"
#include "Engine/Navmesh/NavPower.h"
#include "System/Math/Line.h"

//-------------------------------------------------------------------------

namespace EE::AI
{
    // 判断移动到目标点这个动作,是否正在运行中
    bool MoveToAction::IsRunning() const
    {
        #if EE_ENABLE_NAVPOWER
        return m_path.IsValid();
        #else
        return false;
        #endif
    }

    void MoveToAction::Start( BehaviorContext const& ctx, Vector const& goalPosition )
    {
        // 角色进入运动状态的动画
        ctx.m_pAnimationController->SetCharacterState( CharacterAnimationState::Locomotion );

        //-------------------------------------------------------------------------

        #if EE_ENABLE_NAVPOWER
        auto spaceHandle = ctx.m_pNavmeshSystem->GetSpaceHandle();

        bfx::PathSpec pathSpec;
        pathSpec.m_snapMode = bfx::SNAP_CLOSEST;

        // 路径参数
        bfx::PathCreationOptions pathOptions;
        pathOptions.m_forceFirstPosOntoNavGraph = true;

        // 基于导航网格的寻路
        m_path = bfx::CreatePolylinePath( spaceHandle,
            Navmesh::ToBfx( ctx.m_pCharacter->GetPosition() ),
            Navmesh::ToBfx( goalPosition ), 0, pathSpec, pathOptions );
        if ( m_path.IsValid() )
        {
            m_currentPathSegmentIdx = 0;
            m_progressAlongSegment = 0.0f;
        }
        else
        {
            m_currentPathSegmentIdx = InvalidIndex;
        }
        #endif
    }

    // 在开启导航的情况下, 需要基于路径行走
    void MoveToAction::Update( BehaviorContext const& ctx )
    {
        #if EE_ENABLE_NAVPOWER
        if ( !m_path.IsValid() )
        {
            return;
        }

        // 移动速度(常量)
        float const moveSpeed = 5.5f;
        // 每帧的移动距离 = 移动速度 * 移动时间
        float distanceToMove = moveSpeed * ctx.GetDeltaTime();

        //-------------------------------------------------------------------------

        // 获取角色的面向方向
        Vector facingDir = ctx.m_pCharacter->GetForwardVector();
        EE_ASSERT( m_currentPathSegmentIdx != InvalidIndex );
        // 获取当前路段
        bfx::SurfaceSegment const* pCurrentSegment = m_path.GetSurfaceSegment( m_currentPathSegmentIdx );
        // 当前路段的起点,终点
        Vector const currentSegmentStartPos = Navmesh::FromBfx( pCurrentSegment->GetStartPos() );
        Vector const currentSegmentEndPos = Navmesh::FromBfx( pCurrentSegment->GetEndPos() );
        // 当前位置
        Vector currentPosition;
        if ( !currentSegmentStartPos.IsNearEqual3( currentSegmentEndPos ) )
        {
            // 如果此线段不是很短(起点和终点非常近的话),就根据线段和角色位置,计算出当前位置
            currentPosition = Line( Line::StartEnd, currentSegmentStartPos, currentSegmentEndPos ).GetClosestPointOnLine( ctx.m_pCharacter->GetPosition() );
        }
        else
        {
            // 否则当前位置就直接用起点
            currentPosition = currentSegmentStartPos;
        }

        // Find the goal position for this frame
        //-------------------------------------------------------------------------
        // 目标位置: 计算出当前帧的目标位置
        Vector goalPosition;

        // 是否到达本路段的终点?
        bool atEndOfPath = false;
        // 循环处理是因为一帧可能会走出多个路段的数据之和
        while ( distanceToMove > 0 )
        {
            // 判断当前线段是否为最后一段
            bool const isLastSegment = m_currentPathSegmentIdx == ( m_path.GetNumSegments() - 1 );

            bfx::SurfaceSegment const* pSegment = m_path.GetSurfaceSegment( m_currentPathSegmentIdx );
            Vector segmentStart( Navmesh::FromBfx( pSegment->GetStartPos() ) );
            Vector segmentEnd( Navmesh::FromBfx( pSegment->GetEndPos() ) );

            // Handle zero length segments 特殊处理 0 长度的线段(结束本路段行走)
            Vector const segmentVector( segmentEnd - segmentStart );
            if ( segmentVector.IsZero3() )
            {
                distanceToMove = 0.0f;
                m_progressAlongSegment = 1.0f;
                goalPosition = segmentEnd;

                if( isLastSegment )
                {
                    atEndOfPath = true;
                }

                break;
            }

            //-------------------------------------------------------------------------

            // 路段方向
            Vector segmentDir;
            // 路段长度
            float segmentLength;
            ( segmentEnd - segmentStart ).ToDirectionAndLength3( segmentDir, segmentLength );

            EE_ASSERT( segmentDir.IsNormalized3() );

            // 当前路段已经行走的距离 = 当前路段的长度 * 当前路段的进度系数(0~1 的小数)
            float currentDistanceAlongSegment = segmentLength * m_progressAlongSegment;
            // 当前路段的剩余距离
            float remainingDistance = segmentLength - currentDistanceAlongSegment;

            //-------------------------------------------------------------------------

            // 新的已经行走的距离 = 当前路段已经行走的距离 + 每帧要行走的距离
            float newDistanceAlongSegment = currentDistanceAlongSegment + distanceToMove;
            // 如果不是最后一段 且 已经超过当前线段的距离
            if ( !isLastSegment && newDistanceAlongSegment > segmentLength )
            {
                // 扣除当前路段的剩余距离, 保留还需要行走的距离, 切换到下一个路段
                distanceToMove -= remainingDistance;
                m_progressAlongSegment = 0.0f;
                m_currentPathSegmentIdx++;
            }
            else // Perform full move 在当前路段进行了完整的行走距离
            {
                distanceToMove = 0.0f;
                // 更新当前路段的进度系数
                m_progressAlongSegment = Math::Min( newDistanceAlongSegment / segmentLength, 1.0f );
                // 插值计算目标位置
                goalPosition = Vector::Lerp( segmentStart, segmentEnd, m_progressAlongSegment );
                facingDir = segmentDir;

                if ( isLastSegment && m_progressAlongSegment == 1.0f )
                {
                    atEndOfPath = true;
                }
            }
        }

        //至此, 已经计算出走完这一帧后的目标位置了.

        // Calculate goal pos
        //-------------------------------------------------------------------------

        // 根据目标位置与当前位置的差值,计算当前的速度(插值/时间)
        Vector const desiredDelta = ( goalPosition - currentPosition );
        Vector const headingVelocity = desiredDelta / ctx.GetDeltaTime();
        // 更新面向方向
        facingDir = facingDir.GetNormalized2();

        // 更新运动数据(时间, 速度, 方向)
        auto pLocomotionController = ctx.m_pAnimationController->GetSubGraphController<LocomotionGraphController>();
        pLocomotionController->SetLocomotionDesires( ctx.GetDeltaTime(), headingVelocity, facingDir );

        // Check if we are at the end of the path
        //-------------------------------------------------------------------------
        // 如果已经到达寻路的终点,就结束寻路.

        if ( atEndOfPath )
        {
            m_path.Release();
        }
        #endif
    }
}
