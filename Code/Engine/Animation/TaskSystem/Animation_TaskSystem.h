#pragma once

#include "Animation_Task.h"

//-------------------------------------------------------------------------

namespace EE::Animation
{
    #if EE_DEVELOPMENT_TOOLS
    enum class TaskSystemDebugMode
    {
        Off,
        FinalPose,
        PoseTree,
        DetailedPoseTree
    };
    #endif

    //-------------------------------------------------------------------------

    class TaskSystem
    {
        friend class AnimationDebugView;

    public:

        TaskSystem( Skeleton const* pSkeleton );
        ~TaskSystem();

        void Reset();

        // Get the primary skeleton from this task system
        Skeleton const* GetSkeleton() const { return m_finalPose.GetSkeleton(); }

        // Get the actual final character transform for this frame
        Transform const& GetCharacterWorldTransform() const { return m_taskContext.m_worldTransform; }

        // Get the final pose generated by the task system
        Pose const* GetPose() const{ return &m_finalPose; }

        // Execution
        //-------------------------------------------------------------------------

        // Do we have any tasks that need execution
        inline bool RequiresUpdate() const { return m_needsUpdate; }

        // Do we have a physics dependency in our registered tasks
        inline bool HasPhysicsDependency() const { return m_hasPhysicsDependency; }

        // Run all pre-physics tasks
        void UpdatePrePhysics( float deltaTime, Transform const& worldTransform, Transform const& worldTransformInverse );

        // Run all post-physics tasks and fill out the final pose buffer
        void UpdatePostPhysics();

        // Cached Pose storage
        //-------------------------------------------------------------------------

        inline UUID CreateCachedPose() { return m_posePool.CreateCachedPoseBuffer(); }
        inline void DestroyCachedPose( UUID const& cachedPoseID ) { m_posePool.DestroyCachedPoseBuffer( cachedPoseID ); }

        // Task Registration
        //-------------------------------------------------------------------------

        inline bool HasTasks() const { return m_tasks.size() > 0; }
        inline TVector<Task*> const& GetRegisteredTasks() const { return m_tasks; }

        template< typename T, typename ... ConstructorParams >
        inline TaskIndex RegisterTask( ConstructorParams&&... params )
        {
            EE_ASSERT( m_tasks.size() < 0xFF );
            auto pNewTask = m_tasks.emplace_back( EE::New<T>( std::forward<ConstructorParams>( params )... ) );
            m_hasPhysicsDependency |= pNewTask->HasPhysicsDependency();
            m_needsUpdate = true;
            return (TaskIndex) ( m_tasks.size() - 1 );
        }

        TaskIndex GetCurrentTaskIndexMarker() const { return (TaskIndex) m_tasks.size(); }
        void RollbackToTaskIndexMarker( TaskIndex const marker );

        // Debug
        //-------------------------------------------------------------------------

        #if EE_DEVELOPMENT_TOOLS
        void SetDebugMode( TaskSystemDebugMode mode );
        TaskSystemDebugMode GetDebugMode() const { return m_debugMode; }
        void DrawDebug( Drawing::DrawContext& drawingContext );
        #endif

    private:

        bool AddTaskChainToPrePhysicsList( TaskIndex taskIdx );
        void ExecuteTasks();

        #if EE_DEVELOPMENT_TOOLS
        void CalculateTaskOffset( TaskIndex taskIdx, Float2 const& currentOffset, TInlineVector<Float2, 16>& offsets );
        #endif

    private:

        TVector<Task*>                  m_tasks;
        PoseBufferPool                  m_posePool;
        TaskContext                     m_taskContext;
        TInlineVector<TaskIndex, 16>    m_prePhysicsTaskIndices;
        Pose                            m_finalPose;
        bool                            m_hasPhysicsDependency = false;
        bool                            m_hasCodependentPhysicsTasks = false;
        bool                            m_needsUpdate = false;

        #if EE_DEVELOPMENT_TOOLS
        TaskSystemDebugMode             m_debugMode = TaskSystemDebugMode::Off;
        #endif
    };
}